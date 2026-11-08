require 'spec_helper'
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

      describe '.find_libpython' do
        it 'delegates to FinderForWindows on Windows' do
          allow(Finder).to receive(:windows?).and_return(true)
          expect(FinderForWindows).to receive(:find_libpython).with('python.exe').and_return(:handle)
          expect(Finder).not_to receive(:find_python_config)
          expect(Finder.find_libpython('python.exe')).to eq(:handle)
        end

        it 'does not delegate on the other platforms' do
          allow(Finder).to receive(:windows?).and_return(false)
          expect(FinderForWindows).not_to receive(:find_libpython)
          allow(Finder).to receive(:find_python_config).and_raise(PyCall::PythonNotFound)
          expect { Finder.find_libpython('python3') }.to raise_error(PyCall::PythonNotFound)
        end
      end
    end
  end
end
