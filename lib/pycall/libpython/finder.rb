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

        def find_python_config(python = nil)
          python ||= DEFAULT_PYTHON
          Array(python).each do |python_cmd|
            begin
              python_config = investigate_python_config(python_cmd)
              return [python_cmd, python_config] unless python_config.empty?
            rescue
            end
          end
          raise ::PyCall::PythonNotFound
        end

        def find_libpython(python = nil)
          debug_report("find_libpython(#{python.inspect})")
          python, python_config = find_python_config(python)
          suffix = python_config[:SHLIB_SUFFIX]

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
          suffix = python_config[:SHLIB_SUFFIX]
          if python_config[:LIBRARY]
            ext = File.extname(python_config[:LIBRARY])
            names << python_config[:LIBRARY].delete_suffix(ext) + suffix
            names << python_config[:LIBRARY].delete_suffix(ext) + ".#{LIBSUFFIX}" 
          end
          dlprefix = if windows? then "" else "lib" end
          sysdata = {
            v_major:  python_config[:version_major],
            VERSION:  python_config[:VERSION],
            ABIFLAGS: python_config[:ABIFLAGS],
          }
          [
            "python%{VERSION}%{ABIFLAGS}" % sysdata,
            "python%{VERSION}" % sysdata,
            "python%{v_major}" % sysdata,
            "python"
          ].each do |stem|
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
          IO.popen(python_env, [python, python_investigator_py], 'r') do |io|
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

        private

        def windows?
          Fiddle::WINDOWS
        end

        def apple?
          RUBY_PLATFORM.include?("darwin")
        end

        def dlopen(libname)
          Fiddle.dlopen(libname).tap do |handle|
            debug_report("dlopen(#{libname.inspect}) = #{handle.inspect}") if handle
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
