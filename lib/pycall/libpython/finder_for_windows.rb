require 'pycall/error'
require 'fiddle'

module PyCall
  module LibPython
    # Finds and loads the Python library on Windows.
    #
    # Finder.find_libpython delegates here on Windows, where finding Python
    # and loading its DLL differ from the other platforms:
    #
    # - Python is often not on PATH, as with Anaconda, so CONDA_PREFIX and the
    #   PEP 514 entries in the registry are searched too.
    # - sysconfig has no SHLIB_SUFFIX, and the DLL is named like python311.dll.
    # - The dependencies of the DLL are not found in its own folder.
    module FinderForWindows
      DEFAULT_PYTHON = [
        -'python3',
        -'python',
      ].freeze

      # The location of the PEP 514 registry entries.
      PYTHON_REGISTRY_KEY = -'Software\Python'

      # PEP 514 has companies register side by side.  Prefer the reference
      # implementation like the py launcher does.
      PYTHON_REGISTRY_PREFERRED_COMPANY = -'PythonCore'

      DEFAULT_SHLIB_SUFFIX = -'.dll'

      class << self
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
            normalized = Finder.normalize_path(path, suffix, false)
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
          raise ::PyCall::PythonNotFound
        end

        def find_python_config(python = nil)
          found = if python
                    # An explicitly given python must not be replaced by another.
                    first_python_config(Array(python))
                  else
                    first_python_config(DEFAULT_PYTHON) ||
                      first_python_config(python_candidates)
                  end
          found or raise ::PyCall::PythonNotFound
        end

        # Python executables that can be found without PATH.
        def python_candidates
          (conda_prefix_pythons + registry_pythons).uniq
        end

        def conda_prefix_pythons
          prefix = ENV['CONDA_PREFIX']
          return [] if prefix.nil? || prefix.empty?
          [File.join(prefix, 'python.exe')].select {|path| File.file?(path) }
        end

        # Python executables registered in the registry, best one first.
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

        def investigate_python_config(python)
          python_env = { 'PYTHONIOENCODING' => 'UTF-8' }
          debug_report("investigate_python_config(#{python.inspect})")
          # The Microsoft Store alias python.exe complains on stderr, which would
          # be printed on every start even when a later candidate works.
          popen_options = debug? ? {} : { err: File::NULL }
          IO.popen(python_env, [python, Finder.python_investigator_py], 'r', **popen_options) do |io|
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

        def candidate_names(python_config)
          suffix = shlib_suffix(python_config)
          names = []
          names << python_config[:LDLIBRARY] if python_config[:LDLIBRARY]
          if python_config[:LIBRARY]
            ext = File.extname(python_config[:LIBRARY])
            names << python_config[:LIBRARY].chomp(ext) + suffix
          end
          version = python_config[:VERSION].to_s
          [
            # The name of the DLL has no dot, like python311.dll.
            "python#{version.delete('.')}",
            "python#{version}#{python_config[:ABIFLAGS]}",
            "python#{version}",
            "python",
          ].each do |stem|
            names << "#{stem}#{suffix}"
          end

          names.uniq!

          debug_report("candidate_names: #{names}")
          return names
        end

        def candidate_paths(python_config)
          # The candidate library that linked by executable
          yield python_config[:linked_libpython]

          lib_basenames = candidate_names(python_config)
          Finder.make_libpaths(python_config).each do |dir|
            lib_basenames.each do |name|
              yield File.join(dir, name)
            end
          end
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

        def shlib_suffix(python_config)
          python_config[:SHLIB_SUFFIX] || DEFAULT_SHLIB_SUFFIX
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
          install[:company] == PYTHON_REGISTRY_PREFERRED_COMPANY ? 0 : 1
        end

        # Sorts newer versions first.
        def registry_version_key(install)
          version = install[:sys_version] || install[:tag]
          version.scan(/\d+/).map {|part| -part.to_i }
        end

        def dlopen(libname)
          add_python_dll_directories(libname)
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
          if defined?(RubyInstaller::Runtime)
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
          ENV['PYCALL_DEBUG_FIND_LIBPYTHON'] == '1'
        end
      end
    end
  end
end
