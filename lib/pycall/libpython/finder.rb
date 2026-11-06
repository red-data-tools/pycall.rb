require 'pycall/error'
require 'fiddle'
require 'pathname'

module PyCall
  module LibPython
    module Finder
      case RUBY_PLATFORM
      when /cygwin/
        libprefix = 'cyg'
        libsuffix = 'dll'
      when /mingw/, /mswin/
        libprefix = ''
        libsuffix = 'dll'
      when /darwin/
        libsuffix = 'dylib'
      end

      LIBPREFIX = libprefix || 'lib'
      LIBSUFFIX = libsuffix || 'so'

      class << self
        DEFAULT_PYTHON = [
          -'python3',
          -'python',
        ].freeze

        # The location of the PEP 514 registry entries on Windows.
        PYTHON_REGISTRY_KEY = -'Software\Python'

        # PEP 514 has companies register side by side.  Prefer the reference
        # implementation like the py launcher does, then Anaconda.
        PYTHON_REGISTRY_COMPANIES = [
          -'PythonCore',
          -'ContinuumAnalytics',
        ].freeze

        def find_python_config(python = nil)
          found = first_python_config(Array(python || DEFAULT_PYTHON))
          # An explicitly given python must not be silently replaced by another.
          found ||= first_python_config(windows_python_candidates) if python.nil? && windows?
          found or raise ::PyCall::PythonNotFound
        end

        def find_libpython(python = nil)
          debug_report("find_libpython(#{python.inspect})")
          python, python_config = find_python_config(python)
          suffix = shlib_suffix(python_config)

          use_conda = (ENV.fetch("CONDA_PREFIX", nil) == File.dirname(python_config[:executable]))
          python_home = if !ENV.key?("PYTHONHOME") || use_conda
                          python_config[:PYTHONHOME]
                        else
                          ENV["PYTHONHOME"]
                        end
          ENV["PYTHONHOME"] = python_home

          candidate_paths(python_config) do |path|
            debug_report("Candidate: #{path}")
            normalized = normalize_path(path, suffix)
            if normalized
              debug_report("Trying to dlopen: #{normalized}")
              begin
                return dlopen(normalized)
              rescue Fiddle::DLError
                debug_report "dlopen(#{normalized.inspect}) => #{$!.class}: #{$!.message}"
              end
            else
              debug_report("Not found.")
            end
          end
        end

        def candidate_names(python_config)
          names = []
          names << python_config[:LDLIBRARY] if python_config[:LDLIBRARY]
          suffix = shlib_suffix(python_config)
          if python_config[:LIBRARY]
            ext = File.extname(python_config[:LIBRARY])
            names << python_config[:LIBRARY].delete_suffix(ext) + suffix
          end
          dlprefix = if windows? then "" else "lib" end
          sysdata = {
            v_major:  python_config[:version_major],
            VERSION:  python_config[:VERSION],
            ABIFLAGS: python_config[:ABIFLAGS],
          }
          stems = [
            "python%{VERSION}%{ABIFLAGS}" % sysdata,
            "python%{VERSION}" % sysdata,
            "python%{v_major}" % sysdata,
            "python"
          ]
          # Windows names the library without the dot, like python311.dll.
          stems.unshift("python#{python_config[:VERSION].to_s.delete('.')}") if windows?
          stems.each do |stem|
            names << "#{dlprefix}#{stem}#{suffix}"
          end

          names.compact!
          names.uniq!

          debug_report("candidate_names: #{names}")
          return names
        end

        def candidate_paths(python_config)
          # The candidate library that linked by executable
          yield python_config[:linked_libpython]

          lib_dirs = make_libpaths(python_config)
          lib_basenames = candidate_names(python_config)

          # candidates by absolute paths
          lib_dirs.each do |dir|
            lib_basenames.each do |name|
              yield File.join(dir, name)
            end
          end

          # library names for searching in system library paths
          lib_basenames.each do |name|
            yield name
          end
        end

        def normalize_path(path, suffix, apple_p=apple?)
          return nil if path.nil?
          case
          when path.nil?,
               Pathname.new(path).relative?
            nil
          when File.exist?(path)
            File.realpath(path)
          when File.exist?(path + suffix)
            File.realpath(path + suffix)
          when apple_p
            normalize_path(remove_suffix_apple(path), ".so", false)
          else
            nil
          end
        end

        # Strip off .so or .dylib
        def remove_suffix_apple(path)
          path.sub(/\.(?:dylib|so)\z/, '')
        end

        def investigate_python_config(python)
          python_env = { 'PYTHONIOENCODING' => 'UTF-8' }
          debug_report("investigate_python_config(#{python.inspect})")
          # The Microsoft Store alias python.exe complains on stderr, which would
          # be printed on every start even when a later candidate works.
          popen_options = windows? && !debug? ? { err: File::NULL } : {}
          IO.popen(python_env, [python, python_investigator_py], 'r', **popen_options) do |io|
            {}.tap do |config|
              io.each_line do |line|
                next unless line =~ /: /
                key, value = line.chomp.split(': ', 2)
                case value
                when 'True', 'true', 'False', 'false'
                  value = (value == 'True' || value == 'true')
                end
                config[key.to_sym] = value if value != 'None'
              end
            end
          end
        rescue Errno::ENOENT
          raise PyCall::PythonInvestigationFailed
        end

        def python_investigator_py
          File.expand_path('../../python/investigator.py', __FILE__)
        end

        def make_libpaths(python_config)
          libpaths = python_config.values_at(:LIBPL, :srcdir, :LIBDIR)

          if windows?
            libpaths << File.dirname(python_config[:executable])
          else
            libpaths << File.expand_path('../../lib', python_config[:executable])
          end

          if apple?
            libpaths << python_config[:PYTHONFRAMEWORKPREFIX]
          end

          exec_prefix = python_config[:exec_prefix]
          libpaths << exec_prefix
          libpaths << File.join(exec_prefix, 'lib')

          libpaths.compact!
          libpaths.uniq!

          debug_report "libpaths: #{libpaths.inspect}"
          return libpaths
        end

        # Python executables that can be found on Windows without PATH.
        def windows_python_candidates
          (conda_prefix_pythons + registry_pythons).uniq
        end

        def conda_prefix_pythons
          prefix = ENV['CONDA_PREFIX']
          return [] if prefix.nil? || prefix.empty?
          [File.join(prefix, 'python.exe')].select {|path| File.file?(path) }
        end

        # Python executables registered in the Windows registry, best one first.
        def registry_pythons
          installs = registry_installs.select {|install| registry_install_loadable?(install) }
          sorted = installs.each_with_index.sort_by do |install, index|
            [registry_company_rank(install), registry_version_key(install), index]
          end
          pythons = sorted.map do |install, _|
            install[:executable_path] ||
              (File.join(install[:install_path], 'python.exe') if install[:install_path])
          end
          pythons.compact.select {|path| File.file?(path) }
        end

        # Reads the PEP 514 entries:
        # {HKCU,HKLM}\Software\Python\<company>\<tag>\InstallPath
        def registry_installs(key_path = PYTHON_REGISTRY_KEY)
          require 'win32/registry'
          [
            Win32::Registry::HKEY_CURRENT_USER,
            Win32::Registry::HKEY_LOCAL_MACHINE,
          ].flat_map {|hive| read_registry_installs(hive, key_path) }
        rescue LoadError
          debug_report("Unable to read the registry: #{$!.message}")
          []
        end

        private

        def first_python_config(python_commands)
          python_commands.each do |python_cmd|
            begin
              python_config = investigate_python_config(python_cmd)
              return [python_cmd, python_config] unless python_config.empty?
            rescue
            end
          end
          nil
        end

        # sysconfig has no SHLIB_SUFFIX on Windows.
        def shlib_suffix(python_config)
          python_config[:SHLIB_SUFFIX] || ('.dll' if windows?)
        end

        def read_registry_installs(hive, key_path)
          installs = []
          open_registry_key(hive, key_path) do |root|
            each_registry_subkey(root) do |company, company_key|
              each_registry_subkey(company_key) do |tag, tag_key|
                open_registry_key(tag_key, 'InstallPath') do |install_path_key|
                  installs << {
                    company: company,
                    tag: tag,
                    sys_version: registry_value(tag_key, 'SysVersion'),
                    sys_architecture: registry_value(tag_key, 'SysArchitecture'),
                    install_path: registry_value(install_path_key, ''),
                    executable_path: registry_value(install_path_key, 'ExecutablePath'),
                  }
                end
              end
            end
          end
          installs
        end

        def each_registry_subkey(key)
          key.keys.each do |name|
            open_registry_key(key, name) {|subkey| yield name, subkey }
          end
        end

        # An entry that is missing or unreadable must not hide the others.
        def open_registry_key(key, name, &block)
          key.open(name, &block)
        rescue Win32::Registry::Error
          debug_report("Unable to read the registry key #{name.inspect}: #{$!.message}")
        end

        def registry_value(key, name)
          key[name]
        rescue Win32::Registry::Error
          nil
        end

        # A library for another architecture cannot be loaded into this process.
        def registry_install_loadable?(install)
          bits = Fiddle::SIZEOF_VOIDP * 8
          if install[:sys_architecture]
            install[:sys_architecture] == "#{bits}bit"
          else
            !install[:tag].end_with?(bits == 64 ? '-32' : '-64')
          end
        end

        def registry_company_rank(install)
          PYTHON_REGISTRY_COMPANIES.index(install[:company]) || PYTHON_REGISTRY_COMPANIES.size
        end

        # Sorts newer versions first.
        def registry_version_key(install)
          version = install[:sys_version] || install[:tag]
          version.scan(/\d+/).map {|part| -part.to_i }
        end

        def windows?
          Fiddle::WINDOWS
        end

        def apple?
          RUBY_PLATFORM.include?("darwin")
        end

        def dlopen(libname)
          add_python_dll_directories(libname) if windows?
          Fiddle.dlopen(libname).tap do |handle|
            debug_report("dlopen(#{libname.inspect}) = #{handle.inspect}") if handle
          end
        end

        # Windows does not look for the dependencies of a DLL in the folder of
        # that DLL when it is loaded by an absolute path, and python3xx.dll needs
        # its neighbors like zlib.dll.  Fiddle then reports the misleading
        # "No such file or directory".  Library\bin is the other folder of DLLs
        # in a conda environment, which `conda activate` would add to PATH.
        def add_python_dll_directories(libname)
          dir = File.dirname(libname)
          [dir, File.join(dir, 'Library', 'bin')].select {|path| File.directory?(path) }.reverse_each do |path|
            add_dll_directory(path)
          end
        end

        def add_dll_directory(path)
          debug_report("add_dll_directory(#{path.inspect})")
          if defined?(RubyInstaller::Runtime) && RubyInstaller::Runtime.respond_to?(:add_dll_directory)
            # RubyInstaller excludes PATH from the DLL search.
            RubyInstaller::Runtime.add_dll_directory(path)
          else
            ENV['PATH'] = [path.tr('/', '\\'), ENV['PATH']].compact.join(File::PATH_SEPARATOR)
          end
        end

        def debug_report(message)
          return unless debug?
          $stderr.puts "DEBUG(find_libpython) #{message}"
        end

        def debug?
          @debug ||= (ENV['PYCALL_DEBUG_FIND_LIBPYTHON'] == '1')
        end
      end
    end
  end
end

if __FILE__ == $0
  require "pp"
  python, python_config = PyCall::LibPython::Finder.find_python_config

  puts "python_config:"
  pp python_config

  puts "\ncandidate_names:"
  p PyCall::LibPython::Finder.candidate_names(python_config)

  puts "\nlib_dirs:"
  p PyCall::LibPython::Finder.make_libpaths(python_config)

  puts "\ncandidate_paths:"
  PyCall::LibPython::Finder.candidate_paths(python_config) do |path|
    puts "- #{path}"
  end
end
