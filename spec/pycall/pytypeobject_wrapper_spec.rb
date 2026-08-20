require 'spec_helper'

module PyCall
  ::RSpec.describe PyTypeObjectWrapper do
    let(:simple_class) do
      PyCall.import_module('pycall.simple_class').SimpleClass.__pyptr__
    end

    let(:simple_class_wrapper) do
      PyCall.wrap_class(simple_class)
    end

    describe '#===' do
      specify do
        expect(PyCall.builtins.tuple === PyCall.tuple()).to eq(true)
        np = PyCall.import_module('numpy')
        expect(np.int32 === np.asarray([1], dtype: :int32)[0]).to eq(true)
        expect(np.int64 === np.asarray([1], dtype: :int64)[0]).to eq(true)
        expect(np.integer === np.asarray([1])[0]).to eq(true)
      end
    end

    describe '#<' do
      let(:simple_subclass) do
        PyCall.import_module('pycall.simple_class').SimpleSubClass.__pyptr__
      end

      let(:simple_subclass_wrapper) do
        PyCall.wrap_class(simple_subclass)
      end

      specify do
        expect(simple_subclass_wrapper < simple_class_wrapper).to eq(true)
      end

      specify do
        expect(PyCall.builtins.tuple < simple_class_wrapper).to eq(false)
      end

      specify do
        expect {
          PyCall.builtins.tuple < 0
        }.to raise_error(TypeError, "compared with non class/module")
      end
    end

    describe '.extend_object' do
      context '@__pyptr__ of the extended object is a PyCall::PyTypePtr' do
        it 'extends the given object' do
          cls = Class.new
          cls.instance_variable_set(:@__pyptr__, LibPython::API::PyType_Type)
          expect { cls.extend PyTypeObjectWrapper }.not_to raise_error
          expect(cls).to be_a(PyTypeObjectWrapper)
        end
      end

      context '@__pyptr__ of the extended object is a PyCall::PyPtr' do
        it 'raises TypeError' do
          cls = Class.new
          cls.instance_variable_set(:@__pyptr__, PyPtr::NULL)
          expect { cls.extend PyTypeObjectWrapper }.to raise_error(TypeError, /@__pyptr__/)
          expect(cls).not_to be_a(PyTypeObjectWrapper)
        end
      end

      context '@__pyptr__ of the extended object is nil' do
        it 'raises TypeError' do
          cls = Class.new
          expect { cls.extend PyTypeObjectWrapper }.to raise_error(TypeError, /@__pyptr__/)
          expect(cls).not_to be_a(PyTypeObjectWrapper)
        end
      end

      context '@__pyptr__ of the extended object is not a PyCall::PyPtr' do
        it 'raises TypeError' do
          cls = Class.new
          cls.instance_variable_set(:@__pyptr__, 42)
          expect { cls.extend PyTypeObjectWrapper }.to raise_error(TypeError, /@__pyptr__/)
          expect(cls).not_to be_a(PyTypeObjectWrapper)
        end
      end
    end

    describe '.register_python_type_mapping' do
      pending
    end

    describe '.new' do
      let(:extended_class) { Class.new }

      before do
        extended_class.instance_variable_set(:@__pyptr__, simple_class)
        extended_class.extend PyTypeObjectWrapper
      end

      it 'returns an instance of the extended class object' do
        obj = extended_class.new
        expect(obj).to be_a(extended_class)
        expect(obj.x).to eq(0)
      end

      it 'calls the corresponding Python type object with the given arguments to instantiate its Python object' do
        obj = extended_class.new(42)
        expect(obj.x).to eq(42)
      end

      specify 'the returned object has a Python object pointer whose type is its __pyptr__' do
        obj = extended_class.new
        expect(obj.__pyptr__.__ob_type__).to eq(simple_class_wrapper)
      end
    end
  end
end
