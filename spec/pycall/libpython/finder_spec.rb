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
    end
  end
end
