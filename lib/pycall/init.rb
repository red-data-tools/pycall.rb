module PyCall
  def self.const_missing(name)
    case name
    when :PyPtr, :PyTypePtr, :PyObjectWrapper, :PYTHON_DESCRIPTION, :PYTHON_VERSION
      PyCall.init
      const_get(name)
    else
      super
    end
  end

  module LibPython
    def self.const_missing(name)
      case name
      when :API, :Conversion, :Helpers, :PYTHON_DESCRIPTION, :PYTHON_VERSION
        PyCall.init
        const_get(name)
      else
        super
      end
    end
  end

  def self.init(python = ENV['PYTHON'])
    return false if LibPython.instance_variable_defined?(:@handle)
    class << PyCall
      remove_method :const_missing
    end
    class << PyCall::LibPython
      remove_method :const_missing
    end

    LibPython.instance_variable_set(:@handle, LibPython::Finder.find_libpython(python))
    class << LibPython
      undef_method :handle
      attr_reader :handle
    end

    require 'pycall.so'

    PyCall.sys.path.append(File.expand_path('../python', __FILE__))

    require 'pycall/dict'
    require 'pycall/list'
    require 'pycall/slice'
    const_set(:PYTHON_VERSION, LibPython::PYTHON_VERSION)
    const_set(:PYTHON_DESCRIPTION, LibPython::PYTHON_DESCRIPTION)

    @init_thread_id = Thread.current.object_id
    at_exit do
      # Finalize is only safe to call if we're on the same thread that initialized PyCall
      finalize if Thread.current.object_id == @init_thread_id
    end

    true
  end

  def self.finalize
    LibPython::API.Py_FinalizeEx()
  end
end
