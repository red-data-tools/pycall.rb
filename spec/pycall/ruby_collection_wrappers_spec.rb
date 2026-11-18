require 'spec_helper'
require 'set'

RSpec.describe PyCall do
  let(:helper) { PyCall.import_module('pycall.ruby_collections_test') }
  let(:builtins) { PyCall.builtins }

  describe '.wrap_ruby_collection' do
    it 'selects the wrapper by the type of the object' do
      expect(helper.type_name(PyCall.wrap_ruby_collection([]))).to eq('RubyArray')
      expect(helper.type_name(PyCall.wrap_ruby_collection({}))).to eq('RubyHash')
      expect(helper.type_name(PyCall.wrap_ruby_collection(Set[]))).to eq('RubySet')
    end

    it 'accepts subclasses of Array, Hash, and Set' do
      expect(helper.type_name(PyCall.wrap_ruby_collection(Class.new(Array).new))).to eq('RubyArray')
      expect(helper.type_name(PyCall.wrap_ruby_collection(Class.new(Hash).new))).to eq('RubyHash')
      expect(helper.type_name(PyCall.wrap_ruby_collection(Class.new(Set).new))).to eq('RubySet')
    end

    it 'raises TypeError for any other object' do
      [nil, 1, 'x', :y, 1..2, Object.new].each do |obj|
        expect { PyCall.wrap_ruby_collection(obj) }
          .to raise_error(TypeError, /expected Array, Hash, or Set/)
      end
    end
  end

  describe '.wrap_ruby_collection with an Array' do
    let(:array) { [1, 'x', :y, [2, 3]] }
    subject(:wrapper) { PyCall.wrap_ruby_collection(array) }

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
      expect(helper.equals(wrapper, PyCall.wrap_ruby_collection(array.dup))).to eq(true)
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
      expect(builtins.repr(PyCall.wrap_ruby_collection([1, 'a']))).to eq("RubyArray([1, 'a'])")
    end

    it 'gives back the original Array through ruby_object' do
      expect(wrapper.ruby_object).to equal(array)
    end
  end

  describe '.wrap_ruby_collection with a Hash' do
    let(:hash) { { a: 1, 'b' => [2], 3 => nil } }
    subject(:wrapper) { PyCall.wrap_ruby_collection(hash) }

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
      expect(helper.getitem(PyCall.wrap_ruby_collection(Hash.new(0)), 'z')).to eq('KeyError')
    end

    it 'supports `in`, get(), and dict()' do
      expect(helper.contains(wrapper, 'a')).to eq(true)
      expect(helper.contains(wrapper, 3)).to eq(true)
      expect(helper.contains(wrapper, 'z')).to eq(false)
      expect(helper.get(wrapper, 'z', 42)).to eq(42)
      expect(helper.to_dict(wrapper)).to eq(PyCall::Dict.new('a' => 1, 'b' => [2], 3 => nil))
    end

    it 'can be unpacked as keyword arguments' do
      kwargs = helper.keyword_args(PyCall.wrap_ruby_collection(x: 1, 'y' => 2))
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

  describe '.wrap_ruby_collection with a Set' do
    let(:set) { Set[1, 'x', :y] }
    subject(:wrapper) { PyCall.wrap_ruby_collection(set) }

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
end
