require 'spec_helper'

RSpec.describe PyCall do
  let(:ruby_object_test_module) { PyCall.import_module('pycall.ruby_object_test') }

  describe '.wrap_ruby_object' do
    it 'returns a PyCall::PyRubyPtr' do
      wo = PyCall.wrap_ruby_object(Object.new)
      expect(wo).to be_a(PyCall::PyRubyPtr)
    end

    specify do
      class << (obj = Object.new)
        attr_reader :attr
      end
      obj.instance_variable_set(:@attr, rand)

      pyobj = PyCall.wrap_ruby_object(obj)
      expect(ruby_object_test_module.test_ruby_object_attr(pyobj)).to eq(obj.attr)

      expect { |b|
        obj.define_singleton_method(:smethod, &b)
        ruby_object_test_module.test_ruby_object_method(pyobj)
      }.to yield_with_args(42)
    end

    it 'can be converted to string by repr()' do
      obj = Object.new
      def obj.inspect
        "#<test object>"
      end
      pyobj = PyCall.wrap_ruby_object(obj)
      expect(PyCall.builtins.repr(pyobj)).to eq("<PyCall.ruby_object #<test object>>")
    end

    xit 'guards the ruby object by GC while the wrapper python object is alive' do
      pyobj, obj_id = nil
      tap {
        tap {
          obj = Object.new
          obj.instance_variable_set(:@v, 42)
          pyobj = PyCall.wrap_ruby_object(obj)
          obj_id = obj.__id__
          obj = nil
        }
      }
      GC.start
      expect { ObjectSpace._id2ref(obj_id) }.not_to raise_error

      PyCall::PyPtr.decref(pyobj)
      GC.start
      GC.start # NOTE: just in case
      expect {
        tap {
          tap {
            obj = ObjectSpace._id2ref(obj_id)
            expect(obj).to be_a(Object)
            expect(obj.instance_variable_get(:@v)).to eq(42)
            obj = nil
          }
        }
      }.to raise_error(RangeError)
    end

    context 'the argument is not callable' do
      specify do
        expect {
          f = PyCall.wrap_ruby_object(Object.new)
          ruby_object_test_module.call_callable(f, 42)
        }.to raise_error(PyCall::PyError)
      end
    end

    context 'the argument is callable' do
      specify do
        expect { |b|
          f = PyCall.wrap_ruby_object(b.to_proc)
          ruby_object_test_module.call_callable(f, 42)
        }.to yield_with_args('42')
      end
    end
  end
end
