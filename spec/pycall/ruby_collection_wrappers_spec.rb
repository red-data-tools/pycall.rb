require 'spec_helper'
require 'set'

RSpec.describe PyCall do
  let(:helper) { PyCall.import_module('pycall.ruby_collections_test') }
  let(:builtins) { PyCall.builtins }

  describe '.wrap_as_python_collection' do
    it 'selects the wrapper by the type of the object' do
      expect(helper.type_name(PyCall.wrap_as_python_collection([]))).to eq('RubyArray')
      expect(helper.type_name(PyCall.wrap_as_python_collection({}))).to eq('RubyHash')
      expect(helper.type_name(PyCall.wrap_as_python_collection(Set[]))).to eq('RubySet')
    end

    it 'accepts subclasses of Array, Hash, and Set' do
      expect(helper.type_name(PyCall.wrap_as_python_collection(Class.new(Array).new))).to eq('RubyArray')
      expect(helper.type_name(PyCall.wrap_as_python_collection(Class.new(Hash).new))).to eq('RubyHash')
      expect(helper.type_name(PyCall.wrap_as_python_collection(Class.new(Set).new))).to eq('RubySet')
    end

    it 'raises TypeError for any other object' do
      [nil, 1, 'x', :y, 1..2, Object.new].each do |obj|
        expect { PyCall.wrap_as_python_collection(obj) }
          .to raise_error(TypeError, /expected Array, Hash, or Set/)
      end
    end
  end

  describe '.wrap_as_python_collection with an Array' do
    let(:array) { [1, 'x', :y, [2, 3]] }
    subject(:wrapper) { PyCall.wrap_as_python_collection(array) }

    it 'returns a Python Sequence that is not a list' do
      expect(helper.is_sequence(wrapper)).to eq(true)
      expect(helper.type_name(wrapper)).to eq('RubyArray')
    end

    it 'supports len()' do
      expect(builtins.len(wrapper)).to eq(4)
    end

    it 'converts elements on access with the usual rules' do
      expect(helper.getitem(wrapper, 0)).to eq(1)
      expect(helper.getitem(wrapper, 1)).to eq('x')
      expect(helper.getitem(wrapper, 2)).to eq('y')
      expect(helper.getitem(wrapper, 3).to_a).to eq([2, 3])
    end

    it 'supports negative indices' do
      expect(helper.getitem(wrapper, -1).to_a).to eq([2, 3])
      expect(helper.getitem(wrapper, -4)).to eq(1)
    end

    it 'raises IndexError for an out-of-range index' do
      expect(helper.getitem(wrapper, 4)).to eq('IndexError')
      expect(helper.getitem(wrapper, -5)).to eq('IndexError')
    end

    it 'raises TypeError for a non-integer index' do
      expect(helper.getitem(wrapper, 'x')).to eq('TypeError')
    end

    it 'returns a list for a slice' do
      expect(helper.getslice(wrapper, 1, 3).to_a).to eq(['x', 'y'])
    end

    it 'supports iteration, `in`, and index()' do
      expect(helper.to_list(wrapper).to_a.first(3)).to eq([1, 'x', 'y'])
      expect(helper.contains(wrapper, 'x')).to eq(true)
      expect(helper.contains(wrapper, 'z')).to eq(false)
      expect(helper.index(wrapper, 'y')).to eq(2)
    end

    it 'compares equal to a list with the same elements' do
      expect(helper.equals(wrapper, [1, 'x', 'y', [2, 3]])).to eq(true)
      expect(helper.equals(wrapper, [1, 'x', 'y'])).to eq(false)
      expect(helper.not_equals(wrapper, [1, 'x', 'y'])).to eq(true)
      expect(helper.equals(wrapper, PyCall.wrap_as_python_collection(array.dup))).to eq(true)
    end

    it 'is unhashable like list' do
      expect(helper.is_unhashable(wrapper)).to eq(true)
    end

    it 'reflects later changes of the Ruby Array' do
      wrapper
      array << 5
      expect(builtins.len(wrapper)).to eq(5)
      expect(helper.getitem(wrapper, 4)).to eq(5)
    end

    it 'has repr()' do
      expect(builtins.repr(PyCall.wrap_as_python_collection([1, 'a']))).to eq("RubyArray([1, 'a'])")
    end

    it 'gives back the original Array through ruby_object' do
      expect(wrapper.ruby_object).to equal(array)
    end
  end

  describe '.wrap_as_python_collection with a Hash' do
    let(:hash) { { a: 1, 'b' => [2], 3 => nil } }
    subject(:wrapper) { PyCall.wrap_as_python_collection(hash) }

    it 'returns a Python Mapping that is not a dict' do
      expect(helper.is_mapping(wrapper)).to eq(true)
      expect(helper.type_name(wrapper)).to eq('RubyHash')
    end

    it 'supports len()' do
      expect(builtins.len(wrapper)).to eq(3)
    end

    it 'looks up values, including Symbol keys by str' do
      expect(helper.getitem(wrapper, 'a')).to eq(1)
      expect(helper.getitem(wrapper, 'b').to_a).to eq([2])
      expect(helper.getitem(wrapper, 3)).to eq(nil)
    end

    it 'raises KeyError for a missing key' do
      expect(helper.getitem(wrapper, 'z')).to eq('KeyError')
    end

    it 'does not use the default value of the Hash' do
      expect(helper.getitem(PyCall.wrap_as_python_collection(Hash.new(0)), 'z')).to eq('KeyError')
    end

    it 'supports `in`, get(), and dict()' do
      expect(helper.contains(wrapper, 'a')).to eq(true)
      expect(helper.contains(wrapper, 3)).to eq(true)
      expect(helper.contains(wrapper, 'z')).to eq(false)
      expect(helper.get(wrapper, 'z', 42)).to eq(42)
      expect(helper.to_dict(wrapper)).to eq(PyCall::Dict.new('a' => 1, 'b' => [2], 3 => nil))
    end

    it 'can be unpacked as keyword arguments' do
      kwargs = helper.keyword_args(PyCall.wrap_as_python_collection(x: 1, 'y' => 2))
      expect(kwargs).to eq(PyCall::Dict.new('x' => 1, 'y' => 2))
    end

    it 'compares equal to a dict with the same items' do
      expect(helper.equals(wrapper, PyCall::Dict.new('a' => 1, 'b' => [2], 3 => nil))).to eq(true)
      expect(helper.equals(wrapper, PyCall::Dict.new('a' => 1))).to eq(false)
    end

    it 'is unhashable like dict' do
      expect(helper.is_unhashable(wrapper)).to eq(true)
    end

    it 'reflects later changes of the Ruby Hash' do
      wrapper
      hash[:c] = 4
      expect(builtins.len(wrapper)).to eq(4)
      expect(helper.getitem(wrapper, 'c')).to eq(4)
    end

    it 'gives back the original Hash through ruby_object' do
      expect(wrapper.ruby_object).to equal(hash)
    end
  end

  describe '.wrap_as_python_collection with a Set' do
    let(:set) { Set[1, 'x', :y] }
    subject(:wrapper) { PyCall.wrap_as_python_collection(set) }

    it 'returns a Python Set that is not a set' do
      expect(helper.is_set(wrapper)).to eq(true)
      expect(helper.type_name(wrapper)).to eq('RubySet')
    end

    it 'supports len() and iteration' do
      expect(builtins.len(wrapper)).to eq(3)
      expect(helper.to_list(wrapper).to_a).to contain_exactly(1, 'x', 'y')
    end

    it 'supports `in`, including Symbol elements by str' do
      expect(helper.contains(wrapper, 1)).to eq(true)
      expect(helper.contains(wrapper, 'x')).to eq(true)
      expect(helper.contains(wrapper, 'y')).to eq(true)
      expect(helper.contains(wrapper, 2)).to eq(false)
    end

    it 'returns a Python set from set operations' do
      expect(helper.set_and(wrapper, [1, 2])).to eq(PyCall.eval('{1}'))
      expect(helper.set_type_of_and(wrapper, [1, 2])).to eq('set')
    end

    it 'compares equal to a set with the same elements' do
      expect(helper.equals(wrapper, PyCall.eval("{1, 'x', 'y'}"))).to eq(true)
      expect(helper.equals(wrapper, PyCall.eval('{1}'))).to eq(false)
    end

    it 'is unhashable like set' do
      expect(helper.is_unhashable(wrapper)).to eq(true)
    end

    it 'gives back the original Set through ruby_object' do
      expect(wrapper.ruby_object).to equal(set)
    end
  end

  describe '.wrap_as_python_collection with keys or elements that are equal in Python' do
    let(:hash) { { 'a' => 1, a: 2 } }
    let(:set) { Set[:a, 'a'] }
    let(:ambiguous) { 'ValueError: both "a" and :a are present, and both are "a" in Python' }

    it 'returns the number of Ruby keys or elements from len()' do
      expect(helper.attempt('len', PyCall.wrap_as_python_collection(hash))).to eq(2)
      expect(helper.attempt('len', PyCall.wrap_as_python_collection(set))).to eq(2)
    end

    it 'raises ValueError on iteration' do
      expect(helper.attempt('list', PyCall.wrap_as_python_collection(hash)))
        .to eq("ValueError: two Ruby keys are equal to 'a' in Python")
      expect(helper.attempt('dict', PyCall.wrap_as_python_collection(hash))).to start_with('ValueError: ')
      expect(helper.attempt('list', PyCall.wrap_as_python_collection(set)))
        .to eq("ValueError: two Ruby elements are equal to 'a' in Python")
    end

    it 'raises ValueError when a str matches both a String and a Symbol' do
      wrapper = PyCall.wrap_as_python_collection(hash)
      expect(helper.attempt('getitem', wrapper, 'a')).to eq(ambiguous)
      expect(helper.attempt('contains', wrapper, 'a')).to eq(ambiguous)
      expect(helper.attempt('get', wrapper, 'a', 0)).to eq(ambiguous)
      expect(helper.attempt('contains', PyCall.wrap_as_python_collection(set), 'a')).to eq(ambiguous)
    end

    it 'raises ValueError on comparison' do
      expect(helper.attempt('eq', PyCall.wrap_as_python_collection(hash), PyCall::Dict.new('a' => 2)))
        .to start_with('ValueError: ')
      expect(helper.attempt('eq', PyCall.wrap_as_python_collection(set), PyCall.eval("{'a'}")))
        .to eq("ValueError: two Ruby elements are equal to 'a' in Python")
      expect(helper.attempt('le', PyCall.wrap_as_python_collection(set), PyCall.eval("{'a'}")))
        .to eq("ValueError: two Ruby elements are equal to 'a' in Python")
    end

    it 'detects keys or elements other than Strings and Symbols that are equal in Python' do
      expect(helper.attempt('list', PyCall.wrap_as_python_collection({ 1 => :x, 1.0 => :y })))
        .to eq('ValueError: two Ruby keys are equal to 1.0 in Python')
      expect(helper.attempt('list', PyCall.wrap_as_python_collection(Set[1, 1.0])))
        .to eq('ValueError: two Ruby elements are equal to 1.0 in Python')
    end

    it 'does not affect a String or a Symbol alone' do
      expect(helper.attempt('getitem', PyCall.wrap_as_python_collection({ 'a' => 1 }), 'a')).to eq(1)
      expect(helper.attempt('getitem', PyCall.wrap_as_python_collection({ a: 2 }), 'a')).to eq(2)
      expect(helper.attempt('contains', PyCall.wrap_as_python_collection(Set[:a]), 'a')).to eq(true)
    end
  end

  describe '.wrap_as_python_collection iteration' do
    it 'converts each element of a Set when the iteration reaches it' do
      first = [1]
      second = [2]
      items = helper.first_two(PyCall.wrap_as_python_collection(Set[first, second]),
                               -> { first << 10; second << 20 })
      expect(items.to_a.map(&:to_a)).to eq([[1], [2, 20]])
    end

    it 'converts each key of a Hash when the iteration reaches it' do
      first = [1]
      second = [2]
      items = helper.first_two(PyCall.wrap_as_python_collection({ first => :x, second => :y }),
                               -> { first << 10; second << 20 })
      expect(items.to_a.map(&:to_a)).to eq([[1], [2, 20]])
    end

    it 'iterates over the keys present when the iteration started' do
      hash = { a: 1, b: 2 }
      items = helper.first_two(PyCall.wrap_as_python_collection(hash), -> { hash.delete(:b) })
      expect(items.to_a).to eq(['a', 'b'])
    end
  end

  describe '.wrap_as_python_collection with a subclass whose methods raise' do
    let(:array_class) do
      Class.new(Array) do
        def size
          raise 'size failed'
        end
      end
    end
    let(:hash_class) do
      Class.new(Hash) do
        def key?(*)
          raise ArgumentError, 'key? failed'
        end

        def keys
          raise 'keys failed'
        end
      end
    end
    let(:set_class) do
      Class.new(Set) do
        def include?(*)
          raise NotImplementedError, 'include? failed'
        end

        def to_a
          raise 'to_a failed'
        end
      end
    end

    it 'raises RuntimeError with the Ruby exception for an Array' do
      wrapper = PyCall.wrap_as_python_collection(array_class[1])
      message = 'RuntimeError: Ruby raised RuntimeError: size failed'
      expect(helper.attempt('len', wrapper)).to eq(message)
      expect(helper.attempt('getitem', wrapper, 0)).to eq(message)
      expect(helper.attempt('list', wrapper)).to eq(message)
    end

    it 'raises RuntimeError with the Ruby exception for a Hash, instead of treating the key as missing' do
      wrapper = PyCall.wrap_as_python_collection(hash_class['k' => 1])
      message = 'RuntimeError: Ruby raised ArgumentError: key? failed'
      expect(helper.attempt('getitem', wrapper, 'k')).to eq(message)
      expect(helper.attempt('contains', wrapper, 'k')).to eq(message)
      expect(helper.attempt('get', wrapper, 'k', 42)).to eq(message)
      expect(helper.attempt('list', wrapper)).to eq('RuntimeError: Ruby raised RuntimeError: keys failed')
    end

    it 'raises RuntimeError with the Ruby exception for a Set, including NotImplementedError' do
      wrapper = PyCall.wrap_as_python_collection(set_class[1])
      expect(helper.attempt('contains', wrapper, 1))
        .to eq('RuntimeError: Ruby raised NotImplementedError: include? failed')
      expect(helper.attempt('list', wrapper)).to eq('RuntimeError: Ruby raised RuntimeError: to_a failed')
    end

    it 'does not leave the Ruby exception to be raised later' do
      wrapper = PyCall.wrap_as_python_collection(set_class[1])
      expect { helper.attempt('list', wrapper) }.not_to raise_error
      expect { builtins.len([]) }.not_to raise_error
    end
  end
end
