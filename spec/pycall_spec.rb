require 'spec_helper'

RSpec.describe PyCall do
  it "has a version number" do
    expect(PyCall::VERSION).not_to be nil
  end

  describe 'PYTHON_VERSION' do
    it "has a Python's version number" do
      expect(PyCall::PYTHON_VERSION).to be_kind_of(String)
    end
  end

  describe 'LibPython::API::None' do
    subject { PyCall::LibPython::API::None }
    it { is_expected.to be_a(PyCall::PyPtr) }
    specify { expect(subject.none?).to eq(true) }
    it { is_expected.to be_nil }
    # XXX: it { is_expected.not_to equal(PyCall.eval('None', conversion: false)) }
    # XXX: specify { expect(PyCall::PyObject.new(subject)).to eq(PyCall.eval('None', conversion: false)) }
  end

  describe '.builtins' do
    subject { PyCall.builtins }

    it 'is a wrapper object of Python\'s builtins module' do
      expect(subject).to be_a(Module)
      expect(subject).to be_a(PyCall::PyObjectWrapper)
      expect(subject.__name__).to eq(PyCall::LibPython::PYTHON_VERSION >= '3' ? 'builtins' : '__builtin__')
    end
  end

  describe '.callable?' do
    it 'detects whether the given object is callable' do
      expect(PyCall.callable?(PyCall.builtins.str)).to eq(true)
      expect(PyCall.callable?(PyCall.builtins.object.new)).to eq(false)
      expect(PyCall.callable?(PyCall::LibPython::API::PyDict_Type)).to eq(true)
      expect(PyCall.callable?(PyCall::Dict.new('a' => 1))).to eq(false)
      expect { PyCall.callable?('42') }.to raise_error(TypeError, /unexpected argument type String/)
    end
  end

  describe '.dir' do
    it 'returns a list object containing the attribute names of the given Python object' do
      result = PyCall.dir(PyCall.builtins.object)
      expect(result).to be_a(PyCall::List)
      expect(result).to include('__class__')
    end
  end

  describe '.getattr' do
    let(:pyobj) { PyCall.import_module('pycall.simple_module') }

    specify do
      expect(PyCall.getattr(pyobj, :answer)).to eq(42)
      expect {
        PyCall.getattr(pyobj, :absent_name)
      }.to raise_error(PyCall::PyError, /AttributeError/)
      o = Object.new
      expect(PyCall.getattr(pyobj, :absent_name, o)).to equal(o)
    end
  end

  describe '.hasattr?' do
    let(:pyobj) { PyCall.import_module('pycall.simple_module') }

    specify do
      expect(PyCall.hasattr?(pyobj, :answer)).to eq(true)
      expect(PyCall.hasattr?(pyobj, :absent_name)).to eq(false)
    end
  end

  describe '.same?' do
    let(:simple_module) { PyCall.import_module('pycall.simple_module') }
    let(:simple_class) { PyCall.import_module('pycall.simple_class').SimpleClass }

    specify do
      pyobj_1 = simple_class.new
      expect(PyCall.same?(pyobj_1, pyobj_1)).to eq(true)
      pyobj_2 = simple_module.identity(pyobj_1)
      expect(pyobj_2).not_to be_equal(pyobj_1)
      expect(PyCall.same?(pyobj_1, pyobj_2)).to eq(true)
      expect(PyCall.same?(pyobj_1, simple_class.new)).to eq(false)
    end
  end

  describe '.import_module' do
    subject { PyCall.import_module('sys') }

    it 'returns a wrapper object of Python module with the specified name' do
      expect(subject).to be_a(Module)
      expect(subject).to be_a(PyCall::PyObjectWrapper)
      expect(subject.__name__).to eq('sys')
    end
  end

  describe '.sys' do
    describe '.path' do
      subject { PyCall.sys.path }
      it { is_expected.to be_a(PyCall::List) }
    end
  end

  describe '.wrap_class' do
    let(:python_class) do
      PyCall.import_module('fractions').Fraction.__pyptr__
    end

    it 'returns a new wrapper class' do
      expect(PyCall.wrap_class(python_class)).to be_a(Class)
    end

    it 'extends the resulting wrapper class by PyTypeObjectWrapper' do
      expect(PyCall.wrap_class(python_class)).to be_a(PyCall::PyTypeObjectWrapper)
    end

    it 'returns the first-created wrapper class when called twice' do
      expect(PyCall.wrap_class(python_class)).to equal(PyCall.wrap_class(python_class))
    end
  end

  describe '.wrap_module' do
    subject { PyCall.wrap_module(PyCall::LibPython::API.builtins_module_ptr) }

    it 'returns a Module that wraps a Python object' do
      expect(subject).to be_a(Module)
      expect(subject).to be_a(PyCall::PyObjectWrapper)
      expect(subject.__pyptr__.__address__).to eq(PyCall::LibPython::API.builtins_module_ptr.__address__)
    end

    it 'returns the first-created wrapper module when called twice' do
      expect(PyCall.wrap_module(PyCall::LibPython::API.builtins_module_ptr)).to equal(subject)
    end

    specify 'the wrapped module object can respond to read attributes' do
      # NOTE: `PyCall::LibPython::Helpers.import_module` methods returns the result of `wrap_module` method
      sys = PyCall::LibPython::Helpers.import_module('sys')
      expect(sys.respond_to?(:copyright)).to eq(true)
    end

    specify 'the wrapped module object can respond to callable attributes as methods' do
      pyobj = subject.object
      expect(pyobj).to be_a(PyCall::PyTypeObjectWrapper)
      expect(pyobj).to eq(PyCall::LibPython::Helpers.getattr(subject.__pyptr__, :object))
    end
  end

  describe '.init' do
    it 'returns true if initialization was succeeded' do
      out, err, status = ruby(<<RUBY)
require 'pycall'
puts(PyCall.init ? 'true' : 'false')
RUBY
      expect(status).to be_success
      expect(out.lines.last.chomp).to eq('true')
    end

    it 'returns false if already initialized' do
      out, err, status = ruby(<<RUBY)
require 'pycall'
PyCall.init
puts(PyCall.init ? 'true' : 'false')
RUBY
      expect(status).to be_success
      expect(out.chomp).to eq('false')
    end

    it 'raises PyCall::PythonNotFound error if unable to find libpython library' do
      out, err, status = ruby(<<RUBY)
require 'pycall'
PyCall.init('./invalid-python-path')
RUBY
      expect(status).not_to be_success
      expect(err.chomp).to match(/PyCall::PythonNotFound/)
    end
  end
end
