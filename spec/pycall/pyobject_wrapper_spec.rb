require 'spec_helper'

module PyCall
  ::RSpec.describe PyObjectWrapper do
    let(:simple_class_wrapper) do
      PyCall.import_module('pycall.simple_class').SimpleClass
    end

    describe '.extend_object' do
      context '@__pyptr__ of the extended object is a PyCall::PyPtr' do
        it 'extends the given object' do
          obj = Object.new
          obj.instance_variable_set(:@__pyptr__, PyPtr::NULL)
          expect { obj.extend PyObjectWrapper }.not_to raise_error
          expect(obj).to be_a(PyObjectWrapper)
        end
      end

      context '@__pyptr__ of the extended object is nil' do
        it 'raises TypeError' do
          obj = Object.new
          expect { obj.extend PyObjectWrapper }.to raise_error(TypeError, /@__pyptr__/)
          expect(obj).not_to be_a(PyObjectWrapper)
        end
      end

      context '@__pyptr__ of the extended object is not a PyCall::PyPtr' do
        it 'raises TypeError' do
          obj = Object.new
          obj.instance_variable_set(:@__pyptr__, 42)
          expect { obj.extend PyObjectWrapper }.to raise_error(TypeError, /@__pyptr__/)
          expect(obj).not_to be_a(PyObjectWrapper)
        end
      end
    end

    describe '#method_missing' do
      context 'the Python object has the attribute of the given name' do
        context 'the value of the attribute is not callable' do
          it 'returns the Python object from the attribute' do
            sys = PyCall.import_module('sys')
            expect(sys.copyright).to eq(LibPython::Helpers.getattr(sys.__pyptr__, :copyright))
          end
        end

        context 'the value of the attribute is callable' do
          context 'the value of the attribute is a type object' do
            it 'returns the Python object from the attribute' do
              expect(simple_class_wrapper.NestedClass).to be_a(PyTypeObjectWrapper)
            end
          end

          context 'the value of the attribute is not a type object' do
            it 'returns the result of calling the Python object from the attribute' do
              re = PyCall.import_module('re')
              expect(re.escape('+')).to eq('\\+')
            end

            specify 'initialize attribute is mapped †o __initialize__ in Ruby side' do
              obj = simple_class_wrapper.new(11)
              expect(obj.x).to eq(11)
              expect(obj.initialize(42)).to eq('initialized')
              expect(obj.x).to eq(42)
            end
          end
        end
      end

      context 'the Python object does not have the attribute of the given name' do
        it 'raises NameError' do
          sys = PyCall.import_module('sys')
          expect { sys.not_defined_name }.to raise_error(NameError)
        end
      end

      context 'when the name is :+' do
        it 'delegates to :__add__'
      end

      context 'when the name is :-' do
        it 'delegates to :__sub__'
      end

      context 'when the name is :*' do
        it 'delegates to :__mul__'
      end

      context 'when the name is :/' do
        it 'delegates to :__truediv__'
      end

      context 'when the name is :%' do
        it 'delegates to :__mod__'
      end

      context 'when the name is :**' do
        it 'delegates to :__pow__'
      end

      context 'when the name is :<<' do
        it 'delegates to :__lshift__'
      end

      context 'when the name is :>>' do
        it 'delegates to :__rshift__'
      end

      context 'when the name is :&' do
        it 'delegates to :__and__'
      end

      context 'when the name is :^' do
        it 'delegates to :__xor__'
      end

      context 'when the name is :|' do
        it 'delegates to :__or__'
      end
    end

    describe '#==' do
      pending
    end

    describe '#!=' do
      pending
    end

    describe '#<' do
      pending
    end

    describe '#<=' do
      pending
    end

    describe '#>' do
      pending
    end

    describe '#>=' do
      pending
    end

    describe '#[]' do
      context 'when the given index is a Range' do
        specify do
          list = PyCall::List.new([*1..10])
          expect(list[0..0]).to eq(PyCall::List.new([1]))
          expect(list[0...0]).to eq(PyCall::List.new([]))
          expect(list[0..-1]).to eq(PyCall::List.new([*1..10]))
          expect(list[nil..nil]).to eq(PyCall::List.new([*1..10]))
          expect(list[0...-1]).to eq(PyCall::List.new([*1..9]))
          expect(list[1..-2]).to eq(PyCall::List.new([*2..9]))
          expect(list[1...-2]).to eq(PyCall::List.new([*2..8]))
        end
      end

      context 'when the given index is an Enumerable that is created by Range#step' do
        specify do
          list = PyCall::List.new([*1..10])
          expect(list[(1..-1).step(2)]).to eq(PyCall::List.new([2, 4, 6, 8, 10]))
          expect(list[(1..-2).step(2)]).to eq(PyCall::List.new([2, 4, 6, 8]))
          expect(list[(10..1).step(-1)]).to eq(PyCall::List.new([*1..10].reverse))
          expect(list[(-1..0).step(-1)]).to eq(PyCall::List.new([*1..10].reverse))
          expect(list[(-1...0).step(-1)]).to eq(PyCall::List.new([*2..10].reverse))
          expect(list[(-2..2).step(-2)]).to eq(PyCall::List.new([9, 7, 5, 3]))
          expect(list[(-2...2).step(-2)]).to eq(PyCall::List.new([9, 7, 5]))
        end
      end
    end

    describe '#[]=' do
      context 'when the given index is a Range' do
        specify do
          list = PyCall::List.new([*1..10])
          list[1..-2] = [100, 200, 300]
          expect(list).to eq(PyCall::List.new([1, 100, 200, 300, 10]))

          list = PyCall::List.new([*1..10])
          list[1...-2] = [100, 200, 300]
          expect(list).to eq(PyCall::List.new([1, 100, 200, 300, 9, 10]))
        end
      end

      context 'when the given index is an Enumerable that is created by Range#step' do
        specify do
          list = PyCall::List.new([*1..10])
          list[(1..-3).step(2)] = [100, 200, 300, 400]
          expect(list).to eq(PyCall::List.new([1, 100, 3, 200, 5, 300, 7, 400, 9, 10]))

          list = PyCall::List.new([*1..10])
          list[(1...-3).step(2)] = [100, 200, 300]
          expect(list).to eq(PyCall::List.new([1, 100, 3, 200, 5, 300, 7, 8, 9, 10]))
        end
      end
    end

    describe '#call' do
      context 'when the receiver is callable' do
        subject { PyCall.builtins.object }

        specify do
          expect { subject.() }.not_to raise_error
        end
      end

      context 'when the receiver is not callable' do
        subject { PyCall.builtins.object.new }

        specify do
          expect { subject.() }.to raise_error(TypeError)
        end
      end
    end

    describe '#coerce' do
      let(:np) { PyCall.import_module('numpy') }
      specify do
        x = np.random.randn(10)
        expect(10 * x).to be_a(x.class)
      end
    end

    describe '#dup' do
      subject(:list) { PyCall::List.new([1, 2, 3]) }

      it 'returns a duped instance with a copy of the Python object' do
        duped = list.dup
        expect(duped).not_to equal(list)
        expect(duped.__pyptr__).not_to eq(list.__pyptr__)
        expect(duped).to eq(list)
      end
    end

    describe '#kind_of?' do
      specify do
        expect(PyCall.tuple()).to be_a(PyCall.builtins.tuple)
      end
    end

    describe '#to_s' do
      subject(:dict) { PyCall::Dict.new(a: 1, b: 2, c: 3) }

      it 'returns a string generated by global function str' do
        items = dict.map {|k, v| "'#{k}': #{v}" }
        expect(dict.to_s).to eq("{#{items.join(', ')}}")
      end
    end

    describe '#to_i' do
      it 'delegates to builtins.int'
    end

    describe '#to_f' do
      it 'delegates to builtins.float'
    end
  end
end
