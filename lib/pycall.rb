module PyCall
  require 'pycall/version'
  require 'pycall/libpython'
  require 'pycall/pyerror'
  require 'pycall/pyobject_wrapper'
  require 'pycall/pytypeobject_wrapper'
  require 'pycall/pymodule_wrapper'
  require 'pycall/iterable_wrapper'
  require 'pycall/init'

  module_function

  def builtins
    @builtins ||= wrap_module(LibPython::API.builtins_module_ptr)
  end

  def callable?(obj)
    case obj
    when PyObjectWrapper
      builtins.callable(obj.__pyptr__)
    when PyPtr
      builtins.callable(obj)
    else
      raise TypeError, "unexpected argument type #{obj.class} (expected PyCall::PyPtr or its wrapper)"
    end
  end

  def dir(obj)
    case obj
    when PyObjectWrapper
      builtins.dir(obj.__pyptr__)
    when PyPtr
      builtins.dir(obj)
    else
      raise TypeError, "unexpected argument type #{obj.class} (expected PyCall::PyPtr or its wrapper)"
    end
  end

  def eval(expr, globals: nil, locals: nil)
    globals ||= import_module(:__main__).__dict__
    builtins.eval(expr, globals, locals)
  end

  def exec(code, globals: nil, locals: nil)
    globals ||= import_module(:__main__).__dict__
    if PYTHON_VERSION >= '3'
      builtins.exec(code, globals, locals)
    else
      import_module('PyCall.six').exec_(code, globals, locals)
    end
  end

  def getattr(*args)
    obj, *rest = args
    LibPython::Helpers.getattr(obj.__pyptr__, *rest)
  end

  def hasattr?(obj, name)
    LibPython::Helpers.hasattr?(obj.__pyptr__, name)
  end

  def setattr(obj, name, val)
    LibPython::Helpers.setattr(obj.__pyptr__, name, val)
  end

  def delattr(obj, name)
    LibPython::Helpers.delattr(obj.__pyptr__, name)
  end

  def same?(left, right)
    case left
    when PyObjectWrapper
      case right
      when PyObjectWrapper
        return left.__pyptr__ == right.__pyptr__
      end
    end
    false
  end

  def import_module(name)
    LibPython::Helpers.import_module(name)
  end

  def iterable(obj)
    IterableWrapper.new(obj)
  end

  def len(obj)
    case obj
    when PyObjectWrapper
      builtins.len(obj.__pyptr__)
    when PyPtr
      builtins.len(obj)
    else
      raise TypeError, "unexpected argument type #{obj.class} (expected PyCall::PyPtr or its wrapper)"
    end
  end

  def sys
    @sys ||= import_module('sys')
  end

  def tuple(iterable=nil)
    pyptr = if iterable
              builtins.tuple.(iterable)
            else
              builtins.tuple.()
            end
    Tuple.wrap_pyptr(pyptr)
  end

  def with(ctx)
    begin
      yield ctx.__enter__
    rescue PyError => err
      raise err unless ctx.__exit__(err.type, err.value, err.traceback)
    rescue Exception => err
      # TODO: support telling what exception has been caught
      raise err unless ctx.__exit__(err.class, err, err.backtrace_locations)
    else
      ctx.__exit__(nil, nil, nil)
    end
  end
end

require 'pycall/iruby_helper' if defined? IRuby
