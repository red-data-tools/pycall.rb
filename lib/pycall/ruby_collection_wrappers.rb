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

  # Returns a read-only Python view of the given Array that implements
  # collections.abc.Sequence. Unlike the automatic conversion to a Python
  # list, the elements are converted when Python accesses them.
  def wrap_ruby_array(array)
    unless array.is_a?(Array)
      raise TypeError, "unexpected argument type #{array.class} (expected Array)"
    end
    ruby_collections.RubyArray.(wrap_ruby_object(array))
  end

  # Returns a read-only Python view of the given Hash that implements
  # collections.abc.Mapping. Unlike the automatic conversion to a Python
  # dict, the values are converted when Python accesses them.
  def wrap_ruby_hash(hash)
    unless hash.is_a?(Hash)
      raise TypeError, "unexpected argument type #{hash.class} (expected Hash)"
    end
    ruby_collections.RubyHash.(wrap_ruby_object(hash))
  end

  # Returns a read-only Python view of the given Set that implements
  # collections.abc.Set.
  def wrap_ruby_set(set)
    unless set.is_a?(Set)
      raise TypeError, "unexpected argument type #{set.class} (expected Set)"
    end
    ruby_collections.RubySet.(wrap_ruby_object(set))
  end

  def ruby_collections
    @ruby_collections ||= import_module('PyCall.ruby_collections').tap do |mod|
      mod._setup(wrap_ruby_object(RubyCollectionAccessor))
    end
  end
  private_class_method :ruby_collections
end
