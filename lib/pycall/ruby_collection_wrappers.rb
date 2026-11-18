require 'set'

module PyCall
  # Helper methods called from the Python classes in PyCall.ruby_collections.
  #
  # A Ruby exception raised in a Ruby method called from Python is not
  # propagated to Python, so these methods report a missing element with a
  # `[found, value]` pair and never raise.
  module RubyCollectionAccessor
    module_function

    def collection_length(collection)
      collection.size
    end

    def array_fetch(array, index)
      return [false, nil] unless index.is_a?(Integer) && 0 <= index && index < array.size
      [true, array[index]]
    end

    # A Symbol key is converted to a Python str, so a String key that is not
    # found is also looked up as a Symbol.
    def hash_fetch(hash, key)
      return [true, hash[key]] if hash.key?(key)
      if key.is_a?(String)
        sym = key.to_sym
        return [true, hash[sym]] if hash.key?(sym)
      end
      [false, nil]
    rescue StandardError
      [false, nil]
    end

    def hash_has_key(hash, key)
      hash_fetch(hash, key)[0]
    end

    def hash_keys(hash)
      hash.keys
    end

    def set_include(set, item)
      return true if set.include?(item)
      item.is_a?(String) && set.include?(item.to_sym)
    rescue StandardError
      false
    end

    def set_to_a(set)
      set.to_a
    end
  end

  module_function

  # Returns a read-only Python view of the given Array, Hash, or Set,
  # selected by the type of the object:
  #
  # - Array: PyCall.ruby_collections.RubyArray (collections.abc.Sequence)
  # - Hash:  PyCall.ruby_collections.RubyHash (collections.abc.Mapping)
  # - Set:   PyCall.ruby_collections.RubySet (collections.abc.Set)
  #
  # Unlike the automatic conversion to a Python list or dict, the elements
  # are converted when Python accesses them.
  def wrap_ruby_collection(collection)
    wrapper_class =
      case collection
      when Array then ruby_collections.RubyArray
      when Hash then ruby_collections.RubyHash
      when Set then ruby_collections.RubySet
      else
        raise TypeError, "unexpected argument type #{collection.class} (expected Array, Hash, or Set)"
      end
    wrapper_class.(wrap_ruby_object(collection))
  end

  def ruby_collections
    @ruby_collections ||= import_module('PyCall.ruby_collections').tap do |mod|
      mod._setup(wrap_ruby_object(RubyCollectionAccessor))
    end
  end
  private_class_method :ruby_collections
end
