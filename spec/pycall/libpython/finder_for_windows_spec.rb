require 'spec_helper'
require 'fileutils'
require 'tmpdir'

# Finding Python outside of PATH and loading its DLL are tested with real
# installations by the Windows workflow.  These cover what it cannot set up.
module PyCall
  module LibPython
    ::RSpec.describe FinderForWindows, if: Fiddle::WINDOWS do
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
        it 'does not replace an explicitly given python' do
          allow(FinderForWindows).to receive(:investigate_python_config).and_return({})
          expect(FinderForWindows).not_to receive(:python_candidates)
          expect { FinderForWindows.find_python_config('C:\Missing\python.exe') }.to raise_error(PyCall::PythonNotFound)
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

        it 'skips entries that cannot be loaded into this process' do
          python = python_in('python')
          allow(FinderForWindows).to receive(:registry_installs).and_return([
            install('PythonCore', '3.11', executable_path: python, sys_architecture: "#{other_bits}bit"),
            install('PythonCore', "3.10-#{other_bits}", executable_path: python),
            install('PythonCore', '3.9', install_path: File.join(@tmpdir, 'gone')),
          ])
          expect(FinderForWindows.registry_pythons).to eq([])
        end

        it 'prefers PythonCore, and then newer versions' do
          old_core = python_in('core39')
          new_core = python_in('core311')
          other = python_in('other')
          allow(FinderForWindows).to receive(:registry_installs).and_return([
            install('Other', '3.14', executable_path: other),
            install('PythonCore', '3.9', executable_path: old_core),
            install('PythonCore', '3.11', install_path: File.dirname(new_core)),
          ])
          expect(FinderForWindows.registry_pythons).to eq([new_core, old_core, other])
        end
      end

      describe 'loading the library without RubyInstaller' do
        around do |example|
          saved = ENV['PATH']
          example.run
          ENV['PATH'] = saved
        end

        before do
          hide_const('RubyInstaller')
          allow(Fiddle).to receive(:dlopen).and_return(:handle)
        end

        it 'prepends the folders to PATH with backslashes, the folder of the DLL first' do
          bin = File.join(@tmpdir, 'Library', 'bin')
          FileUtils.mkdir_p(bin)
          before_path = ENV['PATH']

          FinderForWindows.send(:dlopen, "#{@tmpdir}/python311.dll")

          expected = [@tmpdir, bin].map {|path| path.tr('/', '\\') } + [before_path]
          expect(ENV['PATH']).to eq(expected.join(File::PATH_SEPARATOR))
        end
      end
    end
  end
end
