require 'spec_helper'
require 'fileutils'
require 'tmpdir'

module PyCall
  module LibPython
    ::RSpec.describe Finder do
      describe '.find_python_config' do
        context 'when the given python command is not python' do
          specify do
            expect {
              Finder.find_python_config('echo')
            }.to raise_error(PyCall::PythonNotFound)
          end
        end
      end

      describe '.investigate_python_config' do
        subject { Finder.investigate_python_config('python') }

        it { is_expected.to be_a(Hash) }
      end

      # Everything except the real registry access runs on all platforms, so that
      # the Windows behavior is also covered by the CI on Linux and macOS.
      context 'on Windows' do
        before do
          allow(Finder).to receive(:windows?).and_return(true)
        end

        def touch(*path)
          File.join(*path).tap do |file|
            FileUtils.mkdir_p(File.dirname(file))
            FileUtils.touch(file)
          end
        end

        around do |example|
          Dir.mktmpdir('pycall-finder') do |dir|
            @tmpdir = dir
            example.run
          end
        end

        describe '.find_python_config' do
          let(:config) { { executable: 'python.exe' } }

          before do
            allow(Finder).to receive(:investigate_python_config).and_return({})
          end

          it 'prefers python on PATH' do
            allow(Finder).to receive(:investigate_python_config).with('python3').and_return(config)
            expect(Finder).not_to receive(:windows_python_candidates)
            expect(Finder.find_python_config).to eq(['python3', config])
          end

          it 'falls back to the candidates outside of PATH' do
            allow(Finder).to receive(:windows_python_candidates).and_return(['C:\Conda\python.exe'])
            allow(Finder).to receive(:investigate_python_config).with('C:\Conda\python.exe').and_return(config)
            expect(Finder.find_python_config).to eq(['C:\Conda\python.exe', config])
          end

          it 'does not replace an explicitly given python' do
            expect(Finder).not_to receive(:windows_python_candidates)
            expect { Finder.find_python_config('C:\Missing\python.exe') }.to raise_error(PyCall::PythonNotFound)
          end

          it 'raises PythonNotFound when no candidate works' do
            allow(Finder).to receive(:windows_python_candidates).and_return([])
            expect { Finder.find_python_config }.to raise_error(PyCall::PythonNotFound)
          end
        end

        describe '.conda_prefix_pythons' do
          around do |example|
            saved = ENV['CONDA_PREFIX']
            example.run
            ENV['CONDA_PREFIX'] = saved
          end

          it 'finds python.exe in CONDA_PREFIX' do
            python = touch(@tmpdir, 'python.exe')
            ENV['CONDA_PREFIX'] = @tmpdir
            expect(Finder.conda_prefix_pythons).to eq([python])
          end

          it 'ignores a CONDA_PREFIX without python.exe' do
            ENV['CONDA_PREFIX'] = @tmpdir
            expect(Finder.conda_prefix_pythons).to eq([])
          end

          it 'ignores a missing CONDA_PREFIX' do
            ENV.delete('CONDA_PREFIX')
            expect(Finder.conda_prefix_pythons).to eq([])
          end
        end

        describe '.registry_pythons' do
          def install(company, tag, **values)
            { company: company, tag: tag }.merge(values)
          end

          def python_in(name)
            touch(@tmpdir, name, 'python.exe')
          end

          let(:bits) { Fiddle::SIZEOF_VOIDP * 8 }
          let(:other_bits) { bits == 64 ? 32 : 64 }

          it 'finds Anaconda registered under ContinuumAnalytics' do
            python = python_in('anaconda')
            allow(Finder).to receive(:registry_installs).and_return([
              install('ContinuumAnalytics', 'Anaconda311-64', executable_path: python),
            ])
            expect(Finder.registry_pythons).to eq([python])
          end

          it 'derives python.exe from InstallPath without ExecutablePath' do
            python = python_in('core')
            allow(Finder).to receive(:registry_installs).and_return([
              install('PythonCore', '3.11', install_path: File.dirname(python)),
            ])
            expect(Finder.registry_pythons).to eq([python])
          end

          it 'skips entries without an existing executable' do
            allow(Finder).to receive(:registry_installs).and_return([
              install('PythonCore', '3.11', install_path: File.join(@tmpdir, 'gone')),
              install('PythonCore', '3.10'),
            ])
            expect(Finder.registry_pythons).to eq([])
          end

          it 'skips libraries that cannot be loaded into this process' do
            python = python_in('other')
            allow(Finder).to receive(:registry_installs).and_return([
              install('PythonCore', '3.11', executable_path: python, sys_architecture: "#{other_bits}bit"),
              install('PythonCore', "3.10-#{other_bits}", executable_path: python),
            ])
            expect(Finder.registry_pythons).to eq([])
          end

          it 'keeps an entry of the architecture of this process' do
            python = python_in('same')
            allow(Finder).to receive(:registry_installs).and_return([
              install('PythonCore', "3.11-#{other_bits}", executable_path: python, sys_architecture: "#{bits}bit"),
            ])
            expect(Finder.registry_pythons).to eq([python])
          end

          it 'orders by company and then by descending version' do
            old_core = python_in('core39')
            new_core = python_in('core311')
            conda = python_in('conda')
            other = python_in('other')
            allow(Finder).to receive(:registry_installs).and_return([
              install('Other', '3.14', executable_path: other),
              install('ContinuumAnalytics', 'Anaconda3-64', executable_path: conda, sys_version: '3.13'),
              install('PythonCore', '3.9', executable_path: old_core),
              install('PythonCore', '3.11', executable_path: new_core),
            ])
            expect(Finder.registry_pythons).to eq([new_core, old_core, conda, other])
          end
        end

        describe '.candidate_names' do
          # This is what the investigator reports for Python 3.11 of Anaconda.
          let(:config) { { VERSION: '3.11' } }

          it 'uses .dll without SHLIB_SUFFIX, and the name without the dot' do
            expect(Finder.candidate_names(config).first).to eq('python311.dll')
          end

          it 'keeps the names from sysconfig first' do
            names = Finder.candidate_names(config.merge(LDLIBRARY: 'python3.11.dll'))
            expect(names.first).to eq('python3.11.dll')
            expect(names).to include('python311.dll')
          end

          it 'handles LIBRARY without SHLIB_SUFFIX' do
            names = Finder.candidate_names(config.merge(LIBRARY: 'python311.lib'))
            expect(names.first).to eq('python311.dll')
          end
        end

        describe 'loading the library' do
          let(:runtime) { double('RubyInstaller::Runtime', add_dll_directory: nil) }

          before do
            allow(Fiddle).to receive(:dlopen).and_return(:handle)
          end

          context 'with RubyInstaller' do
            before do
              stub_const('RubyInstaller::Runtime', runtime)
            end

            it 'registers the folder of the DLL and Library\bin before loading' do
              bin = File.join(@tmpdir, 'Library', 'bin')
              FileUtils.mkdir_p(bin)
              calls = []
              allow(runtime).to receive(:add_dll_directory) {|path| calls << path }
              allow(Fiddle).to receive(:dlopen) { calls << :dlopen; :handle }

              expect(Finder.send(:dlopen, "#{@tmpdir}/python311.dll")).to eq(:handle)
              expect(calls.last).to eq(:dlopen)
              expect(calls[0...-1]).to contain_exactly(@tmpdir, bin)
            end

            it 'does not register a Library\bin that does not exist' do
              Finder.send(:dlopen, "#{@tmpdir}/python311.dll")
              expect(runtime).to have_received(:add_dll_directory).once.with(@tmpdir)
            end
          end

          context 'without RubyInstaller' do
            around do |example|
              saved = ENV['PATH']
              example.run
              ENV['PATH'] = saved
            end

            before do
              hide_const('RubyInstaller')
            end

            it 'prepends the folders to PATH with backslashes, the folder of the DLL first' do
              bin = File.join(@tmpdir, 'Library', 'bin')
              FileUtils.mkdir_p(bin)
              before_path = ENV['PATH']

              Finder.send(:dlopen, "#{@tmpdir}/python311.dll")

              expected = [@tmpdir, bin].map {|path| path.tr('/', '\\') } + [before_path]
              expect(ENV['PATH']).to eq(expected.join(File::PATH_SEPARATOR))
            end
          end
        end
      end

      context 'not on Windows' do
        before do
          allow(Finder).to receive(:windows?).and_return(false)
        end

        it 'does not look outside of PATH' do
          allow(Finder).to receive(:investigate_python_config).and_return({})
          expect(Finder).not_to receive(:windows_python_candidates)
          expect { Finder.find_python_config }.to raise_error(PyCall::PythonNotFound)
        end

        it 'does not add the name without the dot' do
          names = Finder.candidate_names(VERSION: '3.11', SHLIB_SUFFIX: '.so')
          expect(names).to include('libpython3.11.so')
          expect(names).not_to include('libpython311.so')
        end

        it 'does not touch the DLL search path' do
          allow(Fiddle).to receive(:dlopen).and_return(:handle)
          expect(Finder).not_to receive(:add_dll_directory)
          Finder.send(:dlopen, '/usr/lib/libpython3.11.so')
        end
      end

      describe '.registry_installs', if: Fiddle::WINDOWS do
        # Uses a scratch key instead of Software\Python, so that the specs neither
        # depend on nor change what is installed on the machine.
        let(:key_path) { "Software\\PyCallSpec-#{Process.pid}-#{rand(1 << 32)}" }

        before do
          require 'win32/registry'
        end

        after do
          Win32::Registry::HKEY_CURRENT_USER.delete_key(key_path, true)
        end

        def write_entry(path, values)
          Win32::Registry::HKEY_CURRENT_USER.create("#{key_path}\\#{path}") do |key|
            values.each {|name, value| key[name] = value }
          end
        end

        def installs
          Finder.registry_installs(key_path)
        end

        it 'reads the entries of all companies' do
          write_entry('ContinuumAnalytics\Anaconda311-64', 'SysVersion' => '3.11', 'SysArchitecture' => '64bit')
          write_entry('ContinuumAnalytics\Anaconda311-64\InstallPath',
                      '' => 'C:\Anaconda3', 'ExecutablePath' => 'C:\Anaconda3\python.exe')
          write_entry('PythonCore\3.12\InstallPath', '' => 'C:\Python312')

          expect(installs).to contain_exactly(
            {
              company: 'ContinuumAnalytics',
              tag: 'Anaconda311-64',
              sys_version: '3.11',
              sys_architecture: '64bit',
              install_path: 'C:\Anaconda3',
              executable_path: 'C:\Anaconda3\python.exe',
            },
            {
              company: 'PythonCore',
              tag: '3.12',
              sys_version: nil,
              sys_architecture: nil,
              install_path: 'C:\Python312',
              executable_path: nil,
            },
          )
        end

        it 'ignores entries without InstallPath' do
          write_entry('PyLauncher\Language', 'SysVersion' => '3.11')
          expect(installs).to eq([])
        end

        it 'returns nothing when the key does not exist' do
          write_entry('Unrelated', {})
          expect(Finder.registry_installs("#{key_path}\\Missing")).to eq([])
        end
      end
    end
  end
end
