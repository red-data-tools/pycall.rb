require 'pycall/wrapper_object_cache'

module PyCall
  module PyObjectWrapper
    attr_reader :__pyptr__

    def self.extend_object(obj)
      pyptr = obj.instance_variable_get(:@__pyptr__)
      unless pyptr.kind_of? PyPtr
        raise TypeError, "@__pyptr__ should have PyCall::PyPtr object"
      end
      super
    end

    OPERATOR_METHOD_NAMES = {
      # Unary operators
      :+@ => :__pos__,
      :-@ => :__neg__,
      :~  => :__invert__,

      # Binary operators
      :+  => :__add__,
      :-  => :__sub__,
      :*  => :__mul__,
      :/  => :__truediv__,
      :%  => :__mod__,
      :** => :__pow__,
      :<< => :__lshift__,
      :>> => :__rshift__,
      :&  => :__and__,
      :^  => :__xor__,
      :|  => :__or__
    }.freeze

    def method_missing(name, *args)
      case name
      when *OPERATOR_METHOD_NAMES.keys
        op_name = OPERATOR_METHOD_NAMES[name]
        if LibPython::Helpers.hasattr?(__pyptr__, op_name)
          LibPython::Helpers.define_wrapper_method(self, op_name)
          singleton_class.__send__(:alias_method, name, op_name)
          return self.__send__(name, *args)
        end
      else
        name_without_last_equal = name.to_s.chomp('=')
        if LibPython::Helpers.hasattr?(__pyptr__, name_without_last_equal)
          LibPython::Helpers.define_wrapper_method(self, name)
          return self.__send__(name, *args)
        end
      end
      super
    end

    def respond_to_missing?(name, include_private)
      return true if LibPython::Helpers.hasattr?(__pyptr__, name)
      super
    end

    def kind_of?(cls)
      case cls
      when PyTypeObjectWrapper
        __pyptr__.kind_of?(cls.__pyptr__)
      else
        super
      end
    end

    [:==, :!=, :<, :<=, :>, :>=].each do |op|
      class_eval("#{<<-"begin;"}\n#{<<-"end;"}", __FILE__, __LINE__+1)
      begin;
        def #{op}(other)
          case other
          when PyObjectWrapper
            LibPython::Helpers.compare(:#{op}, __pyptr__, other.__pyptr__)
          else
            other = Conversion.from_ruby(other)
            LibPython::Helpers.compare(:#{op}, __pyptr__, other)
          end
        end
      end;
    end

    def [](*key)
      LibPython::Helpers.getitem(__pyptr__, key)
    end

    def []=(*key, value)
      LibPython::Helpers.setitem(__pyptr__, key, value)
    end

    def call(*args)
      LibPython::Helpers.call_object(__pyptr__, *args)
    end

    class SwappedOperationAdapter
      def initialize(obj)
        @obj = obj
      end

      attr_reader :obj

      def +(other)
        other.__radd__(self.obj)
      end

      def -(other)
        other.__rsub__(self.obj)
      end

      def *(other)
        other.__rmul__(self.obj)
      end

      def /(other)
        other.__rtruediv__(self.obj)
      end

      def %(other)
        other.__rmod__(self.obj)
      end

      def **(other)
        other.__rpow__(self.obj)
      end

      def <<(other)
        other.__rlshift__(self.obj)
      end

      def >>(other)
        other.__rrshift__(self.obj)
      end

      def &(other)
        other.__rand__(self.obj)
      end

      def ^(other)
        other.__rxor__(self.obj)
      end

      def |(other)
        other.__ror__(self.obj)
      end
    end

    def coerce(other)
      [SwappedOperationAdapter.new(other), self]
    end

    def dup
      super.tap do |duped|
        copied = PyCall.import_module('copy').copy(__pyptr__)
        copied = copied.__pyptr__ if copied.kind_of? PyObjectWrapper
        duped.instance_variable_set(:@__pyptr__, copied)
      end
    end

    def inspect
      PyCall.builtins.repr(__pyptr__)
    end

    def to_s
      LibPython::Helpers.str(__pyptr__)
    end

    def to_i
      LibPython::Helpers.call_object(PyCall::builtins.int.__pyptr__, __pyptr__)
    end

    def to_f
      LibPython::Helpers.call_object(PyCall::builtins.float.__pyptr__, __pyptr__)
    end
  end

  module_function

  def check_ismodule(pyptr)
    return if pyptr.kind_of? LibPython::API::PyModule_Type
    raise TypeError, "PyModule object is required"
  end

  def check_isclass(pyptr)
    pyptr = pyptr.__pyptr__ if pyptr.kind_of? PyObjectWrapper
    return if pyptr.kind_of? LibPython::API::PyType_Type
    return if defined?(LibPython::API::PyClass_Type) && pyptr.kind_of?(LibPython::API::PyClass_Type)
    raise TypeError, "PyType object is required"
  end
end
