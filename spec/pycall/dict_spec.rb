require 'spec_helper'

module PyCall
  ::RSpec.describe Dict do
    let(:time_module) { PyCall.import_module('time') }
    let(:key) { 'a' }

    subject(:dict) { Dict.new(key => 1, 'b' => 2, 'c' => 3) }

    describe '.new' do
      let(:key) { time_module.localtime() }

      it 'accepts python object as a key' do
        expect{ Dict.new(key => 1) }.not_to raise_error
      end
    end

    describe '#[]' do
      it 'returns a value corresponding to a given key' do
        expect(subject['a']).to eq(1)
        expect(subject['b']).to eq(2)
        expect(subject['c']).to eq(3)
        expect { subject['nothing'] }.not_to raise_error
      end

      it 'increments the returned python object' do
        pyobj = PyCall.builtins.object.()
        subject['o'] = pyobj
        expect { subject['o'] }.to change { pyobj.__pyptr__.__ob_refcnt__ }.from(2).to(3)
        expect(subject['o'].__pyptr__).to eq(pyobj.__pyptr__)
      end

      context 'when key is a python object' do
        let(:key) { time_module.localtime() }

        it 'returns a value corresponding to a given key' do
          expect(subject[key]).to eq(1)
        end
      end
    end

    describe '#[]=' do
      it 'stores a given value for a given key' do
        subject['a'] *= 10
        expect(subject['a']).to eq(10)

        subject['b'] *= 10
        expect(subject['b']).to eq(20)

        subject['c'] *= 10
        expect(subject['c']).to eq(30)
      end

      context 'when key is a python object' do
        let(:key) { time_module.localtime() }

        it 'stores a given value for a given key' do
          subject[key] *= 10
          expect(subject[key]).to eq(10)
        end
      end
    end

    describe '#delete' do
      it 'deletes a value for a given key'do
        expect(subject.delete('a')).to eq(1)
        expect(subject['b']).to eq(2)
        expect(subject['c']).to eq(3)
        expect(subject['a']).to eq(nil)
      end

      context 'when key is a python object' do
        let(:key) { time_module.localtime() }

        it 'deletes a value for a given key'do
          expect(subject.delete(key)).to eq(1)
          expect(subject['b']).to eq(2)
          expect(subject['c']).to eq(3)
          expect(subject[key]).to eq(nil)
        end
      end
    end

    describe '#has_key?' do
      specify do
        expect(subject).to have_key('a')
        expect(subject).to have_key('b')
        expect(subject).to have_key('c')
        expect(subject).not_to have_key('d')
      end

      context 'when key is a python object' do
        let(:key) { time_module.localtime() }

        specify do
          expect(subject).to have_key(key)
          non_key = time_module.localtime(time_module.time() + 1)
          expect(subject).not_to have_key(non_key)
        end
      end
    end

    describe '#length' do
      subject { dict.length }
      it { is_expected.to eq(3) }
    end

    describe '#each' do
      it 'yields each key-value pair' do
        expect { |b| subject.each(&b) }.to yield_successive_args(['a', 1], ['b', 2], ['c', 3])
      end

      it 'returns an enumerator without a block' do
        expect(subject.each.to_a).to eq([['a', 1], ['b', 2], ['c', 3]])
      end
    end

    describe '#to_h' do
      it 'returns a Hash' do
        expect(subject.to_h).to eq('a' => 1, 'b' => 2, 'c' => 3)
      end
    end

    context 'for an instance of a subclass of dict' do
      let(:helper) { PyCall.import_module('pycall.dict_subclass') }
      let(:collections) { PyCall.import_module('collections') }

      context 'that overrides nothing (like pandas PrettyDict, issue #131)' do
        subject(:dict) { helper.PlainDictSubclass.new(Dict.new('a' => 1, 'b' => 2)) }

        it 'is wrapped as PyCall::Dict' do
          expect(subject).to be_a(Dict)
        end

        specify '#each yields each key-value pair' do
          expect { |b| subject.each(&b) }.to yield_successive_args(['a', 1], ['b', 2])
        end

        specify '#to_h returns a Hash' do
          expect(subject.to_h).to eq('a' => 1, 'b' => 2)
        end

        specify '#has_key?' do
          expect(subject).to have_key('a')
          expect(subject).not_to have_key('z')
        end
      end

      context 'collections.OrderedDict' do
        subject(:dict) { helper.moved_ordered_dict() }

        it 'iterates in the OrderedDict order' do
          expect(subject.each.to_a).to eq([['b', 2], ['c', 3], ['a', 1]])
          expect(subject.to_h.keys).to eq(%w[b c a])
        end
      end

      context 'collections.defaultdict' do
        subject(:dict) { collections.defaultdict.new(PyCall.builtins.int, Dict.new('a' => 1)) }

        it 'does not add a missing key via #has_key?' do
          expect(subject).not_to have_key('z')
          expect(subject.to_h).to eq('a' => 1)
        end
      end

      context 'collections.Counter' do
        subject(:dict) { collections.Counter.new(%w[x y x]) }

        specify do
          expect(subject.to_h).to eq('x' => 2, 'y' => 1)
          expect(subject).to have_key('x')
        end
      end

      context 'that overrides items, __iter__, and __contains__' do
        subject(:dict) { helper.VirtualItemsDict.new(Dict.new('a' => 1)) }

        it 'uses the overridden methods like Python does' do
          expect(subject.each.to_a).to eq([['virtual', 42]])
          expect(subject).to have_key('virtual')
          expect(subject).not_to have_key('a')
        end
      end

      context 'whose items() does not return pairs' do
        subject(:dict) { helper.BadItemsDict.new(Dict.new('a' => 1)) }

        it 'raises TypeError' do
          expect { subject.each {} }.to raise_error(TypeError, /expected a 2-tuple/)
        end
      end

      it 'can break out of #each' do
        dict = helper.PlainDictSubclass.new(Dict.new('a' => 1, 'b' => 2))
        expect(dict.each { |k, v| break k }).to eq('a')
      end
    end
  end
end
