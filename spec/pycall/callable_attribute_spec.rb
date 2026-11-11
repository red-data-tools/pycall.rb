require 'spec_helper'

# GitHub issue #87: Ruby cannot distinguish `obj.attr` from `obj.attr()`.
# When no argument is given, a callable attribute is called only if it is a
# routine (function, method, etc.).  Other callable objects are returned as
# they are.
RSpec.describe 'callable attributes of Python objects' do
  let(:mod) { PyCall.import_module('pycall.callable_attribute') }

  context 'when the attribute is a callable instance (like scipy.stats.norm)' do
    it 'returns the object itself when no argument is given' do
      norm = mod.norm
      expect(norm).to be_a(PyCall::PyObjectWrapper)
      expect(norm).to be_kind_of(mod.DistributionGenerator)
    end

    it 'allows calling a method of the object with all its arguments' do
      expect(mod.norm.pdf([1, 2, 3], 0, 10)).to eq('generator pdf([1, 2, 3]) loc=0 scale=10')
    end

    it 'calls the object when arguments are given' do
      frozen = mod.norm(0, 10)
      expect(frozen).to be_kind_of(mod.FrozenDistribution)
      expect(frozen.pdf(5)).to eq('frozen pdf(5) loc=0 scale=10')
    end

    it 'calls the object when only keyword arguments are given' do
      frozen = mod.norm(loc: 1, scale: 2)
      expect(frozen.pdf(5)).to eq('frozen pdf(5) loc=1 scale=2')
    end

    it 'calls the object by #call' do
      frozen = mod.norm.()
      expect(frozen.pdf(5)).to eq('frozen pdf(5) loc=0 scale=1')
    end

    it 'returns the object from a class attribute and an instance attribute' do
      target = mod.Target.new
      expect(mod.Target.norm.pdf(1, 2, 3)).to eq('generator pdf(1) loc=2 scale=3')
      expect(target.norm.pdf(1, 2, 3)).to eq('generator pdf(1) loc=2 scale=3')
      expect(target.instance_norm.pdf(1, 2, 3)).to eq('generator pdf(1) loc=2 scale=3')
    end
  end

  context 'when the attribute is a routine' do
    it 'calls a module-level function without arguments' do
      expect(mod.zero_arg_function).to eq('function called')
    end

    it 'calls a bound method without arguments' do
      expect(mod.Target.new.zero_arg_method).to eq('method called')
    end

    it 'calls a staticmethod without arguments' do
      expect(mod.Target.zero_arg_staticmethod).to eq('staticmethod called')
      expect(mod.Target.new.zero_arg_staticmethod).to eq('staticmethod called')
    end

    it 'calls a classmethod without arguments' do
      expect(mod.Target.zero_arg_classmethod).to eq('classmethod called on Target')
      expect(mod.Target.new.zero_arg_classmethod).to eq('classmethod called on Target')
    end

    it 'calls a builtin method without arguments' do
      expect(mod.builtin_method).to eq('ABC')
    end

    it 'calls a builtin function without arguments' do
      expect(PyCall.import_module('os').getpid).to eq(Process.pid)
    end

    it 'calls a method-wrapper without arguments' do
      expect(mod.method_wrapper).to eq('42')
    end

    it 'calls a functools.lru_cache wrapper without arguments' do
      expect(mod.cached_zero_arg_function).to eq('cached function called')
    end

    it 'calls a functools.partial object without arguments' do
      expect(mod.partial_function).to eq('partial called with 42')
    end

    it 'calls a callable non-data descriptor without arguments' do
      expect(mod.descriptor_callable).to eq('descriptor called')
    end

    it 'calls a Ruby lambda stored in a Python attribute without arguments' do
      target = mod.Target.new
      PyCall.setattr(target, :callback, -> { 'ruby lambda called' })
      expect(target.callback).to eq('ruby lambda called')
    end
  end
end
