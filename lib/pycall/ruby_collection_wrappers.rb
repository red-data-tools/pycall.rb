require 'set'

module PyCall
  # Helper methods called from the Python classes in PyCall.ruby_collections.
  #
  # A Ruby exception raised in a Ruby method called from Python is not
  # propagated to Python, so each method returns a `[status, value]` pair
  # instead of raising:
  #
  # - OK: the call succeeded, and value is the result.
  # - MISSING: the index, key, or element was not found.
  # - ERROR: a StandardError or a NotImplementedError was raised, and value
  #   is its description.
  # - COLLISION: a String key or element was looked up while both the String
  #   and the Symbol with the same name are present, and value describes them.
  module RubyCollectionAccessor
    OK = 0
    MISSING = 1
    ERROR = 2
    COLLISION = 3

    module_function

    def collection_length(collection)
      guard { [OK, collection.size] }
    end

    # Also used to read the snapshots returned by hash_keys and set_to_a.
    def array_fetch(array, index)
      guard do
        if index.is_a?(Integer) && 0 <= index && index < array.size
          [OK, array[index]]
        else
          [MISSING, nil]
        end
      end
    end

    def hash_fetch(hash, key)
      guard do
        status, resolved = resolve_key(key) { |k| hash.key?(k) }
        status == OK ? [OK, hash[resolved]] : [status, resolved]
      end
    end

    def hash_has_key(hash, key)
      guard do
        status, resolved = resolve_key(key) { |k| hash.key?(k) }
        status == OK ? [OK, nil] : [status, resolved]
      end
    end

    # Returns the keys as an Array wrapped as a Ruby object, so that the
    # bridge does not convert them to a Python list at once.
    def hash_keys(hash)
      guard { [OK, PyCall.wrap_ruby_object(hash.keys)] }
    end

    def set_include(set, item)
      guard do
        status, resolved = resolve_key(item) { |e| set.include?(e) }
        status == OK ? [OK, nil] : [status, resolved]
      end
    end

    # Returns the elements as an Array wrapped as a Ruby object, so that the
    # bridge does not convert them to a Python list at once.
    def set_to_a(set)
      guard { [OK, PyCall.wrap_ruby_object(set.to_a)] }
    end

    # A Symbol is converted to a Python str, so a String that is not found is
    # also looked up as a Symbol.  When both are present, the lookup is
    # ambiguous and reported as COLLISION.
    def resolve_key(key)
      found = yield(key)
      sym = symbol_for(key)
      found_sym = !sym.nil? && yield(sym)
      if found && found_sym
        [COLLISION, "both #{key.inspect} and #{sym.inspect} are present, " \
                    "and both are #{key.inspect} in Python"]
      elsif found
        [OK, key]
      elsif found_sym
        [OK, sym]
      else
        [MISSING, nil]
      end
    end

    def symbol_for(key)
      return nil unless key.is_a?(String)
      key.to_sym
    rescue EncodingError
      nil
    end

    def guard
      yield
    rescue StandardError, NotImplementedError => e
      [ERROR, "#{e.class}: #{e.message}"]
    end

    private_class_method :resolve_key, :symbol_for, :guard
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
  def wrap_as_python_collection(collection)
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
