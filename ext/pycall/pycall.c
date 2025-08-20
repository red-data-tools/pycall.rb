#include "pycall_internal.h"
#include "pycall.h"
#include <ruby/encoding.h>

#include <stdarg.h>

VALUE pycall_mPyCall;

VALUE mLibPython;
VALUE mAPI;
VALUE mHelpers;
VALUE mConversion;
VALUE mPyObjectWrapper;
VALUE mPyTypeObjectWrapper;
VALUE mGCGuard;

VALUE pycall_cPyPtr;
VALUE cPyTypePtr;
VALUE cTuple;
VALUE cPyError;

VALUE pycall_eError;

static VALUE pycall_libpython_handle;
static VALUE python_description;
static VALUE python_version_string;
static Py_ssize_t python_hexversion;
static int python_major_version;
static int python_has_stackless_extension;
static PyObject *python_builtins_module;
static VALUE python_type_mapping;
static ID id_python_type_mapping;

int
pycall_python_major_version(void)
{
  return python_major_version;
}

Py_ssize_t
pycall_python_hexversion(void)
{
  return python_hexversion;
}

#undef pycall_python_major_version
#define pycall_python_major_version() python_major_version

#undef pycall_python_hexversion
#define pycall_python_hexversion() python_hexversion

#define python_is_unicode_literals (python_major_version >= 3)

intptr_t pycall_hash_salt;

static VALUE pycall_call_python_callable(PyObject *pycallable, int argc, VALUE *argv);

#define PyType_Check(pyobj) PyType_FastSubclass(Py_TYPE(pyobj), Py_TPFLAGS_TYPE_SUBCLASS)
#define PyClass_Check(pyobj) (Py_API(PyClass_Type) && (pyobj)->ob_type == Py_API(PyClass_Type))

#define PyObject_Hash(pyobj) (pycall_python_long_hash ? Py_API(PyObject_Hash._long)(pyobj) : Py_API(PyObject_Hash._hash_t)(pyobj))

/* ==== PyCall ==== */

VALUE
pycall_after_fork(VALUE mod)
{
  Py_API(PyOS_AfterFork)();
  return Qnil;
}

static volatile pycall_tls_key without_gvl_key;

int
pycall_without_gvl_p(void)
{
  /*
   * In pthread, the default value is NULL (== 0).
   *
   * In Win32 thread, the default value is 0 (initialized by TlsAlloc).
   */
  return pycall_tls_get(without_gvl_key) != (void*)0;
}

static inline int
pycall_set_without_gvl(void)
{
  return pycall_tls_set(without_gvl_key, (void *)1);
}

static inline int
pycall_set_with_gvl(void)
{
  return pycall_tls_set(without_gvl_key, (void *)0);
}

VALUE
pycall_without_gvl(VALUE (* func)(VALUE), VALUE arg)
{
  int state;
  VALUE result;

  pycall_set_without_gvl();

  result = rb_protect(func, arg, &state);

  pycall_set_with_gvl();

  if (state) {
    rb_jump_tag(state);
  }

  return result;
}

static VALUE
pycall_m_without_gvl(VALUE mod)
{
  return pycall_without_gvl(rb_yield, Qnil);
}

/* ==== PyCall::PyPtr ==== */

const rb_data_type_t pycall_pyptr_data_type = {
  "PyCall::PyPtr",
  { 0, pycall_pyptr_free, pycall_pyptr_memsize, },
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
  0, 0, RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

void
pycall_pyptr_free(void *ptr)
{
  PyObject *pyobj = ptr;
#ifdef PYCALL_DEBUG_DUMP_REFCNT
  if (pyobj->ob_refcnt == 0) {
    fprintf(stderr, "zero refcnt object %p of type %s\n", pyobj, Py_TYPE(pyobj)->tp_name);
  }
#endif /* PYCALL_DEBUG_DUMP_REFCNT */

  if (Py_API(PyGILState_Check())) {
    pycall_Py_DecRef(pyobj);
  }
  else {
    PyGILState_STATE gstate = Py_API(PyGILState_Ensure)();
    pycall_Py_DecRef(pyobj);
    Py_API(PyGILState_Release)(gstate);
  }
}

static size_t _PySys_GetSizeOf(PyObject *);

size_t
pycall_pyptr_memsize(void const *ptr)
{
  if (ptr)
    return _PySys_GetSizeOf((PyObject *)ptr);

  return 0;
}

static size_t
_PySys_GetSizeOf(PyObject *o)
{
  PyObject *res = NULL;
  Py_ssize_t size;

  if (Py_API(PyType_Ready)(Py_TYPE(o)) < 0)
    return (size_t)-1;

  res = Py_API(PyObject_CallMethod)(o, "__sizeof__", "");
  if (res == NULL)
    return (size_t)-1;

  size = Py_API(PyLong_AsSsize_t)(res);
  pycall_Py_DecRef(res);
  if (size == -1 && Py_API(PyErr_Occurred)())
    return (size_t)-1;

  if (size < 0)
    return (size_t)-1;

  if (PyObject_IS_GC(o)) {
    size += sizeof(PyGC_Head);
  }
  return (size_t)size;
}

static inline int
is_pycall_pyptr(VALUE obj)
{
  return rb_typeddata_is_kind_of(obj, &pycall_pyptr_data_type);
}

static inline PyObject*
get_pyobj_ptr(VALUE obj)
{
  PyObject *pyobj;
  TypedData_Get_Struct(obj, PyObject, &pycall_pyptr_data_type, pyobj);
  return pyobj;
}

PyObject *
pycall_pyptr_get_pyobj_ptr(VALUE pyptr)
{
  return get_pyobj_ptr(pyptr);
}

static inline PyObject*
try_get_pyobj_ptr(VALUE obj)
{
  if (!is_pycall_pyptr(obj)) return NULL;
  return (PyObject*)DATA_PTR(obj);
}

static inline PyObject *
check_get_pyobj_ptr(VALUE obj, PyTypeObject *pytypeobj)
{
  PyObject *pyobj;

  if (!is_pycall_pyptr(obj))
    rb_raise(rb_eTypeError, "unexpected type %s (expected PyCall::PyPtr)", rb_class2name(CLASS_OF(obj)));

  pyobj = get_pyobj_ptr(obj);
  if (pytypeobj && Py_TYPE(pyobj) != pytypeobj)
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected %s)", Py_TYPE(pyobj)->tp_name, pytypeobj->tp_name);

  return pyobj;
}

VALUE
pycall_pyptr_incref(VALUE pyptr)
{
  PyObject *pyobj;

  pyobj = try_get_pyobj_ptr(pyptr);
  if (pyobj)
    Py_API(Py_IncRef)(pyobj);
  return pyptr;
}

static VALUE
pycall_pyptr_s_incref(VALUE klass, VALUE pyptr)
{
  return pycall_pyptr_incref(pyptr);
}

void
pycall_Py_DecRef(PyObject *pyobj)
{
#ifdef PYCALL_DEBUG_DUMP_REFCNT
  fprintf(stderr, "decref object %p of type %s, refcnt %"PRIdSIZE"\n",
      pyobj,
      pyobj ? Py_TYPE(pyobj)->tp_name : "nullptr",
      pyobj ? pyobj->ob_refcnt : -1);
#endif /* PYCALL_DEBUG_DUMP_REFCNT */
  Py_API(Py_DecRef)(pyobj);
}

VALUE
pycall_pyptr_decref(VALUE pyptr)
{
  PyObject *pyobj;

  pyobj = try_get_pyobj_ptr(pyptr);
  if (pyobj) {
    pycall_Py_DecRef(pyobj);
    DATA_PTR(pyptr) = NULL;
  }
  return pyptr;
}

static VALUE
pycall_pyptr_s_decref(VALUE klass, VALUE pyptr)
{
  return pycall_pyptr_decref(pyptr);
}

static VALUE
pycall_pyptr_s_sizeof(VALUE klass, VALUE pyptr)
{
  size_t size;
  PyObject *pyobj;

  pyobj = try_get_pyobj_ptr(pyptr);
  if (pyobj == NULL) return Qnil;

  size = _PySys_GetSizeOf(pyobj);
  return SIZET2NUM(size);
}

static VALUE
pycall_pyptr_allocate(VALUE klass)
{
  return TypedData_Wrap_Struct(klass, &pycall_pyptr_data_type, NULL);
}

static inline VALUE
pycall_pyptr_new_with_klass(VALUE klass, PyObject *pyobj)
{
  VALUE obj = pycall_pyptr_allocate(klass);
  DATA_PTR(obj) = pyobj;
#ifdef PYCALL_DEBUG_DUMP_REFCNT
  fprintf(stderr, "%s: object %p of type %s, refcnt=%"PRIdSIZE" (%s:%d)\n",
      rb_class2name(klass), pyobj,
      (pyobj ? Py_TYPE(pyobj)->tp_name : "(nullptr)"),
      (pyobj ? pyobj->ob_refcnt : -1),
      rb_sourcefile(), rb_sourceline());
#endif /* PYCALL_DEBUG_DUMP_REFCNT */
  return obj;
}

VALUE
pycall_pyptr_new(PyObject *pyobj)
{
  return pycall_pyptr_new_with_klass(cPyPtr, pyobj);
}

static VALUE
pycall_pyptr_initialize(VALUE pyptr, VALUE val)
{
  VALUE addr;
  PyObject *pyobj;

  addr = rb_check_to_integer(val, "to_int");
  if (NIL_P(addr)) {
    rb_raise(rb_eTypeError, "Invalid PyObject address: %"PRIsVALUE, val);
  }

  pyobj = (PyObject *)NUM2PTR(addr);
  DATA_PTR(pyptr) = pyobj;
  return pyptr;
}

static VALUE
pycall_pyptr_is_null(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  return pyobj ? Qfalse : Qtrue;
}

static VALUE
pycall_pyptr_is_none(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  return pyobj == Py_API(_Py_NoneStruct) ? Qtrue : Qfalse;
}

static VALUE
pycall_pyptr_is_nil(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  return (pyobj == Py_API(_Py_NoneStruct)) || (pyobj == NULL) ? Qtrue : Qfalse;
}

static VALUE
pycall_pyptr_eq(VALUE obj, VALUE other)
{
  PyObject* pyobj;
  PyObject* pyobj_other;

  if (!is_pycall_pyptr(other)) return Qfalse;

  pyobj = get_pyobj_ptr(obj);
  pyobj_other = get_pyobj_ptr(other);

  return pyobj == pyobj_other ? Qtrue : Qfalse;
}

static VALUE
pycall_pyptr_get_ptr_address(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  return PTR2NUM(pyobj);
}

static VALUE
pycall_pyptr_get_ob_refcnt(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  if (pyobj)
    return SSIZET2NUM(pyobj->ob_refcnt);
  return Qnil;
}

VALUE pycall_pytypeptr_new(PyObject *pytype);

static VALUE
pycall_pyptr_get_ob_type(VALUE obj)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  if (pyobj) {
    VALUE res;
    if (Py_TYPE(pyobj) == Py_API(PyInstance_Type))
      res = pycall_pytype_to_ruby((PyObject *)((PyInstanceObject *)pyobj)->in_class);
    else
      res = pycall_pytype_to_ruby((PyObject *)pyobj->ob_type);
    return res;
  }
  return Qnil;
}

static VALUE
pycall_pyptr_object_id(VALUE obj)
{
  return rb_obj_id(obj);
}

static VALUE
pycall_pyptr_class(VALUE obj)
{
  return CLASS_OF(obj);
}

static VALUE
pycall_pyptr_inspect(VALUE obj)
{
  VALUE cname, str;
  PyObject* pyobj = get_pyobj_ptr(obj);

  cname = rb_class_name(CLASS_OF(obj));
  str = rb_sprintf("#<%"PRIsVALUE":%p type=%s addr=%p>", cname, (void*)obj, Py_TYPE(pyobj)->tp_name, pyobj);

  return str;
}

static VALUE
class_or_module_required(VALUE klass)
{
  if (SPECIAL_CONST_P(klass)) goto not_class;
  switch (BUILTIN_TYPE(klass)) {
    case T_MODULE:
    case T_CLASS:
    case T_ICLASS:
      break;

    default:
    not_class:
      rb_raise(rb_eTypeError, "class or module required");
  }
  return klass;
}

static VALUE
pycall_pyptr_is_kind_of(VALUE obj, VALUE klass)
{
  PyObject* pyobj = get_pyobj_ptr(obj);
  VALUE res;

  if (is_pycall_pyptr(klass)) {
    int res;
    PyObject* pyobj_klass = get_pyobj_ptr(klass);
    res = Py_API(PyObject_IsInstance)(pyobj, pyobj_klass);
    if (res >= 0) {
      return res ? Qtrue : Qfalse;
    }
    Py_API(PyErr_Clear)();
  }

  klass = class_or_module_required(klass);
  res = rb_class_inherited_p(CLASS_OF(obj), klass);
  return NIL_P(res) ? Qfalse : res;
}

static VALUE
pycall_pyptr_hash(VALUE obj)
{
  PyObject *pyobj = get_pyobj_ptr(obj);
  Py_hash_t h;

  if (!pyobj)
    return PTR2NUM(pyobj);

  h = PyObject_Hash(pyobj);
  if (h == -1) {
    Py_API(PyErr_Clear)();
    return PTR2NUM(pyobj);
  }

  return SSIZET2NUM(h);
}

/* ==== PyTypePtr ==== */

const rb_data_type_t pycall_pytypeptr_data_type = {
  "PyCall::PyTypePtr",
  { 0, pycall_pyptr_free, pycall_pyptr_memsize, },
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
  &pycall_pyptr_data_type, 0, RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

static inline int
is_pycall_pytypeptr(VALUE obj)
{
  return rb_typeddata_is_kind_of(obj, &pycall_pytypeptr_data_type);
}

static inline PyTypeObject*
get_pytypeobj_ptr(VALUE obj)
{
  PyTypeObject *pytype;
  TypedData_Get_Struct(obj, PyTypeObject, &pycall_pytypeptr_data_type, pytype);
  return pytype;
}

static inline PyTypeObject*
try_get_pytypeobj_ptr(VALUE obj)
{
  if (!is_pycall_pytypeptr(obj)) return NULL;
  return (PyTypeObject*)DATA_PTR(obj);
}

static inline PyTypeObject *
check_get_pytypeobj_ptr(VALUE obj)
{
  PyTypeObject *pytypeobj;
  if (!is_pycall_pytypeptr(obj))
    rb_raise(rb_eTypeError, "unexpected type %s (expected PyCall::PyTypePtr)", rb_class2name(CLASS_OF(obj)));

  pytypeobj = get_pytypeobj_ptr(obj);
  if (!PyType_Check(pytypeobj))
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected type or class)", Py_TYPE(pytypeobj)->tp_name);

  return pytypeobj;
}

static VALUE
pycall_pytypeptr_allocate(VALUE klass)
{
  return TypedData_Wrap_Struct(klass, &pycall_pytypeptr_data_type, NULL);
}

static inline VALUE
pycall_pytypeptr_new_with_klass(VALUE klass, PyObject *pytypeobj)
{
  VALUE obj = pycall_pytypeptr_allocate(klass);
  DATA_PTR(obj) = pytypeobj;
  return obj;
}

VALUE
pycall_pytypeptr_new(PyObject *pytypeobj)
{
  return pycall_pytypeptr_new_with_klass(cPyTypePtr, pytypeobj);
}

static VALUE
pycall_pytypeptr_get_ob_size(VALUE obj)
{
  PyTypeObject* pytype = get_pytypeobj_ptr(obj);
  if (pytype)
    return SSIZET2NUM(pytype->ob_size);
  return Qnil;
}

static VALUE
pycall_pytypeptr_get_tp_name(VALUE obj)
{
  PyTypeObject* pytype = get_pytypeobj_ptr(obj);
  if (pytype) {
    if (Py_TYPE(pytype) == Py_API(PyType_Type))
      return rb_str_new2(pytype->tp_name);
    return pycall_pyobject_to_ruby(((PyClassObject *)pytype)->cl_name);
  }
  return Qnil;
}

static VALUE
pycall_pytypeptr_get_tp_basicsize(VALUE obj)
{
  PyTypeObject* pytype = get_pytypeobj_ptr(obj);
  if (pytype) {
    if (Py_TYPE(pytype) == Py_API(PyType_Type))
      return SSIZET2NUM(pytype->tp_basicsize);
  }
  return Qnil;
}

static VALUE
pycall_pytypeptr_get_tp_flags(VALUE obj)
{
  PyTypeObject* pytype = get_pytypeobj_ptr(obj);
  if (pytype) {
    if (Py_TYPE(pytype) == Py_API(PyType_Type))
      return ULONG2NUM(pytype->tp_flags);
  }
  return Qnil;
}

static VALUE
pycall_pytypeptr_eqq(VALUE obj, VALUE other)
{
  if (is_pycall_pyptr(other))
    return pycall_pyptr_is_kind_of(other, obj);
  return Qfalse;
}

static VALUE
pycall_pytypeptr_subclass_p(VALUE obj, VALUE other)
{
  PyTypeObject* pytype = get_pytypeobj_ptr(obj);
  if (is_pycall_pyptr(other)) {
    PyTypeObject* pytype_other = try_get_pytypeobj_ptr(other);
    if (pytype_other) {
      int res =  Py_API(PyObject_IsSubclass)((PyObject *)pytype, (PyObject *)pytype_other);
      return res ? Qtrue : Qfalse;
    }
  }
  return Qfalse;
}

/* ==== PyCall::LibPython::API ==== */

static VALUE
pycall_libpython_api_get_builtins_module_ptr(VALUE mod)
{
  VALUE pyptr = pycall_pyptr_new(python_builtins_module);
  Py_API(Py_IncRef)(python_builtins_module);
  return pyptr;
}

static VALUE
pycall_libpython_api_PyObject_Dir(VALUE mod, VALUE pyptr)
{
  PyObject *dir;
  PyObject *pyobj;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);
  dir = Py_API(PyObject_Dir)(pyobj);
  if (pyobj && !dir) {
    pycall_pyerror_fetch_and_raise("PyObject_Dir in pycall_libpython_api_PyObject_Dir");
  }

  return dir ? pycall_pyptr_new(dir) : Qnil;
}

static VALUE
pycall_libpython_api_PyList_Size(VALUE mod, VALUE pyptr)
{
  PyObject *pyobj;
  Py_ssize_t size;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);
  size = Py_API(PyList_Size)(pyobj);
  if (size < 0) {
    pycall_pyerror_fetch_and_raise("PyList_Size in pycall_libpython_api_PyList_Size");
  }

  return SSIZET2NUM(size);
}

static VALUE
pycall_libpython_api_PyList_GetItem(VALUE mod, VALUE pyptr, VALUE idx)
{
  PyObject *pyobj;
  PyObject *pyobj_item;
  Py_ssize_t i;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);
  i = NUM2SSIZET(idx);
  pyobj_item = Py_API(PyList_GetItem)(pyobj, i);
  if (!pyobj_item) {
    pycall_pyerror_fetch_and_raise("PyList_GetItem in pycall_libpython_api_PyList_GetItem");
  }

  return pycall_pyptr_new(pyobj_item);
}

/* ==== PyCall::Helpers ==== */

static VALUE
pycall_libpython_helpers_m_unicode_literals_p(VALUE mod)
{
  return python_is_unicode_literals ? Qtrue : Qfalse;
}

VALUE
pycall_import_module(char const *name)
{
  PyObject *pymod = Py_API(PyImport_ImportModule)(name);
  if (!pymod) {
    pycall_pyerror_fetch_and_raise("PyImport_ImportModule in pycall_libpython_helpers_m_import_module");
  }
  return pycall_pyobject_to_ruby(pymod);
}

VALUE
pycall_import_module_level(char const *name, VALUE globals, VALUE locals, VALUE fromlist, int level)
{
  PyObject *pyglobals = NULL, *pylocals = NULL, *pyfromlist = NULL, *pymod;

  if (!NIL_P(globals)) {
    pyglobals = check_get_pyobj_ptr(globals, Py_API(PyDict_Type));
  }
  if (!NIL_P(locals)) {
    pylocals = check_get_pyobj_ptr(locals, Py_API(PyDict_Type));
  }
  if (!NIL_P(fromlist)) {
    fromlist = rb_convert_type(fromlist, T_ARRAY, "Array", "to_ary");
    pyfromlist = pycall_pyobject_from_ruby(fromlist);
  }
  else {
    /* TODO: set the default fromlist to ['*'] */
  }

  pymod = Py_API(PyImport_ImportModuleLevel)(name, pyglobals, pylocals, pyfromlist, level);
  if (!pymod) {
    pycall_pyerror_fetch_and_raise("PyImport_ImportModuleLevel in pycall_libpython_helpers_m_import_module");
  }

  return pycall_pyobject_to_ruby(pymod);
}

static VALUE
pycall_libpython_helpers_m_import_module(int argc, VALUE *argv, VALUE mod)
{
  VALUE name, globals, locals, fromlist, level;
  char const *name_cstr;

  rb_scan_args(argc, argv, "14", &name, &globals, &locals, &fromlist, &level);

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  name_cstr = StringValueCStr(name);

  if (argc == 1) {
    return pycall_import_module(name_cstr);
  }

  if (argc == 5) {
    level = rb_check_to_integer(level, "to_int");
  }
  else {
    /* TODO: set the default level to 0 */
  }

  return pycall_import_module_level(name_cstr, globals, locals, fromlist, NUM2INT(level));
}

static int
pycall_rich_compare_opid(VALUE op)
{
  ID rb_opid;

  Check_Type(op, T_SYMBOL);
  rb_opid = SYM2ID(op);

  if (rb_opid == '>')
    return Py_GT;
  if (rb_opid == '<')
    return Py_LT;
  if (rb_opid == rb_intern("=="))
    return Py_EQ;
  if (rb_opid == rb_intern("!="))
    return Py_NE;
  if (rb_opid == rb_intern(">="))
    return Py_GE;
  if (rb_opid == rb_intern("<="))
    return Py_LE;

  rb_raise(rb_eArgError, "invalid compare operator: %"PRIsVALUE, op);
}

static VALUE
pycall_libpython_helpers_m_compare(VALUE mod, VALUE op, VALUE pyptr_a, VALUE pyptr_b)
{
  PyObject *pyobj_a, *pyobj_b, *res;
  int opid;

  opid = pycall_rich_compare_opid(op);

  if (!is_pycall_pyptr(pyptr_a)) {
    rb_raise(rb_eTypeError, "unexpected 2nd argument type %s (expected PyCall::PyPtr)", rb_class2name(CLASS_OF(pyptr_a)));
  }
  if (!is_pycall_pyptr(pyptr_b)) {
    rb_raise(rb_eTypeError, "unexpected 3rd argument type %s (expected PyCall::PyPtr)", rb_class2name(CLASS_OF(pyptr_b)));
  }

  pyobj_a = get_pyobj_ptr(pyptr_a);
  pyobj_b = get_pyobj_ptr(pyptr_b);

  res = Py_API(PyObject_RichCompare)(pyobj_a, pyobj_b, opid);
  if (!res) {
    pycall_pyerror_fetch_and_raise("PyObject_RichCompare in pycall_libpython_helpers_m_compare");
  }
  VALUE obj = pycall_pyobject_to_ruby(res);
  pycall_Py_DecRef(res);
  return obj;
}

static int is_pyobject_wrapper(VALUE obj);

VALUE
pycall_getattr_default(VALUE obj, char const *name, VALUE default_value)
{
  PyObject *pyobj, *res;

  if (is_pyobject_wrapper(obj)) {
    pyobj = pycall_pyobject_wrapper_get_pyobj_ptr(obj);
  }
  else {
    pyobj = check_get_pyobj_ptr(obj, NULL);
  }

  res = Py_API(PyObject_GetAttrString)(pyobj, name);
  if (!res && default_value == Qundef) {
    pycall_pyerror_fetch_and_raise("PyObject_GetAttrString in pycall_libpython_helpers_m_getattr");
  }
  Py_API(PyErr_Clear)();
  return res ? pycall_pyobject_to_ruby(res) : default_value;
}

VALUE
pycall_getattr(VALUE pyptr, char const *name)
{
  return pycall_getattr_default(pyptr, name, Qundef);
}

static VALUE
pycall_libpython_helpers_m_getattr(int argc, VALUE *argv, VALUE mod)
{
  VALUE pyptr, name, default_value;

  if (rb_scan_args(argc, argv, "21", &pyptr, &name, &default_value) == 2) {
    default_value = Qundef;
  }

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  return pycall_getattr_default(pyptr, StringValueCStr(name), default_value);
}

static VALUE
pycall_libpython_helpers_m_hasattr_p(VALUE mod, VALUE pyptr, VALUE name)
{
  PyObject *pyobj;
  int res;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  res = Py_API(PyObject_HasAttrString)(pyobj, StringValueCStr(name));
  return res ? Qtrue : Qfalse;
}

static VALUE
pycall_libpython_helpers_m_setattr(VALUE mod, VALUE pyptr, VALUE name, VALUE val)
{
  PyObject *pyobj, *pyval;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  pyval = pycall_pyobject_from_ruby(val);
  if (Py_API(PyObject_SetAttrString)(pyobj, StringValueCStr(name), pyval) == -1) {
    pycall_pyerror_fetch_and_raise("PyObject_SetAttrString");
  }

  return Qnil;
}

static VALUE
pycall_libpython_helpers_m_delattr(VALUE mod, VALUE pyptr, VALUE name)
{
  PyObject *pyobj;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  if (Py_API(PyObject_DelAttrString)(pyobj, StringValueCStr(name)) == -1) {
    pycall_pyerror_fetch_and_raise("PyObject_DelAttrString");
  }

  return Qnil;
}

static VALUE
pycall_libpython_helpers_m_callable_p(VALUE mod, VALUE pyptr)
{
  PyObject *pyobj;
  int res;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);

  res = Py_API(PyCallable_Check)(pyobj);
  return res ? Qtrue : Qfalse;
}

static VALUE
pycall_libpython_helpers_m_call_object(int argc, VALUE *argv, VALUE mod)
{
  VALUE pyptr;
  PyObject *pyobj;

  if (argc < 1) {
    rb_raise(rb_eArgError, "too few arguments (%d for >=1)", argc);
  }

  pyptr = argv[0];
  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);
  if (!Py_API(PyCallable_Check)(pyobj)) {
    rb_raise(rb_eTypeError, "Non-callable Python object was given");
  }

  if (argc == 1) {
    return pycall_call_python_callable(pyobj, 0, NULL);
  }
  else {
    return pycall_call_python_callable(pyobj, argc - 1, argv + 1);
  }
}

static int
pycall_extract_kwargs_from_ruby_hash(VALUE key, VALUE value, VALUE arg)
{
  PyObject *kwargs = (PyObject *)arg;
  char const *key_cstr;
  PyObject *pyvalue;

  if (RB_TYPE_P(key, T_SYMBOL)) {
    key = rb_sym_to_s(key);
  }
  key_cstr = StringValueCStr(key);
  pyvalue = pycall_pyobject_from_ruby(value);

  int res = Py_API(PyDict_SetItemString)(kwargs, key_cstr, pyvalue);
  pycall_Py_DecRef(pyvalue);

  return (res < 0) ? ST_STOP : ST_CONTINUE;
}

static void
pycall_interrupt_python_thread(void *ptr)
{
  Py_API(PyErr_SetInterrupt)();
}

struct call_pyobject_call_params {
  PyObject *pycallable;
  PyObject *args;
  PyObject *kwargs;
};

static inline PyObject *
call_pyobject_call(struct call_pyobject_call_params *params)
{
  PyObject *res;
  res = Py_API(PyObject_Call)(params->pycallable, params->args, params->kwargs); /* New reference */
  return res;
}

PyObject *
pyobject_call(PyObject *pycallable, PyObject *args, PyObject *kwargs)
{
  PyObject *res;
  struct call_pyobject_call_params params;
  params.pycallable = pycallable;
  params.args = args;
  params.kwargs = kwargs;

  if (pycall_without_gvl_p()) {
    res = (PyObject *)rb_thread_call_without_gvl(
        (void * (*)(void *))call_pyobject_call, (void *)&params,
        (rb_unblock_function_t *)pycall_interrupt_python_thread, NULL);
  }
  else {
    res = call_pyobject_call(&params);
  }

  return res;
}

static VALUE
pycall_call_python_callable(PyObject *pycallable, int argc, VALUE *argv)
{
  PyObject *args, *res;
  PyObject *kwargs = NULL;
  Py_ssize_t i, n;
  VALUE hash, obj;

  /* TODO: Use inspect.getfullargspec */

  if (argc > 0) {
    n = argc - RB_TYPE_P(argv[argc - 1], T_HASH);
  }
  else {
    n = 0;
  }

  args = Py_API(PyTuple_New)(n);
  if (!args) {
    pycall_pyerror_fetch_and_raise("PyTuple_New in pycall_call_python_callable");
  }

  for (i = 0; i < n; ++i) {
    PyObject *pytem = pycall_pyobject_from_ruby(argv[i]);
    if (Py_API(PyTuple_SetItem)(args, i, pytem) == -1) {
        pycall_Py_DecRef(pytem);
        pycall_Py_DecRef(args);
        pycall_pyerror_fetch_and_raise("PyTuple_SetItem in pycall_call_python_callable");
    }
    /* NOTE: Although PyTuple_SetItem steals the item reference,
     * it is unnecessary to call Py_IncRef for the item because
     * pycall_pyobject_from_ruby increments the reference count
     * of its result. */
  }

  if (n < argc) {
    hash = argv[argc - 1];
    kwargs = Py_API(PyDict_New)();
    if (!RHASH_EMPTY_P(hash)) {
      rb_hash_foreach(hash, pycall_extract_kwargs_from_ruby_hash, (VALUE)kwargs);
      if (Py_API(PyErr_Occurred)() != NULL) {
        pycall_Py_DecRef(args);
        pycall_pyerror_fetch_and_raise("PyDict_SetItemString in pycall_extract_kwargs_from_ruby_hash");
      }
    }
  }

  res = pyobject_call(pycallable, args, kwargs); /* New reference */
  pycall_Py_DecRef(args);
  if (kwargs) {
      pycall_Py_DecRef(kwargs);
  }
  if (!res) {
    pycall_pyerror_fetch_and_raise("PyObject_Call in pycall_call_python_callable");
  }
  obj = pycall_pyobject_to_ruby(res);
  pycall_Py_DecRef(res);
  return obj;
}

static VALUE
pycall_pyobject_wrapper_wrapper_method(int argc, VALUE *argv, VALUE wrapper)
{
  VALUE pyptr, name;
  char *name_cstr;
  PyObject *pyobj, *attr;

  pyptr = rb_attr_get(wrapper, rb_intern("@__pyptr__"));
  if (NIL_P(pyptr) || !is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "Wrong wrapper object is given");
  }

  pyobj = get_pyobj_ptr(pyptr);

  name = rb_id2str(rb_frame_this_func());
  name_cstr = StringValueCStr(name);

  if (name_cstr[RSTRING_LEN(name) - 1] == '=') {
    int res;
    VALUE val;

    rb_scan_args(argc, argv, "1", &val);

    attr = pycall_pyobject_from_ruby(val);
    if (!attr) {
      pycall_pyerror_fetch_and_raise("pycall_pyobject_from_ruby in pycall_pyobject_wrapper_wrapper_method");
    }

    name_cstr[RSTRING_LEN(name) - 1] = '\0';
    res = Py_API(PyObject_SetAttrString)(pyobj, name_cstr, attr);
    pycall_Py_DecRef(attr);
    name_cstr[RSTRING_LEN(name) - 1] = '=';
    if (res == -1) {
      pycall_pyerror_fetch_and_raise("PyObject_SetAttrString in pycall_pyobject_wrapper_wrapper_method");
    }

    return val;
  }

  attr = Py_API(PyObject_GetAttrString)(pyobj, name_cstr);
  if (!attr) {
    pycall_pyerror_fetch_and_raise("PyObject_GetAttrString in pycall_pyobject_wrapper_wrapper_method");
  }

  if (!Py_API(PyCallable_Check)(attr))
    return pycall_pyobject_to_ruby(attr);

  if (PyType_Check(attr) || PyClass_Check(attr))
    return pycall_pyobject_to_ruby(attr);

  VALUE obj = pycall_call_python_callable(attr, argc, argv);
  pycall_Py_DecRef(attr);
  return obj;
}

static VALUE
pycall_libpython_helpers_m_define_wrapper_method(VALUE mod, VALUE wrapper, VALUE name)
{
  VALUE pyptr;
  PyObject *pyobj, *attr;
  char *name_cstr;

  pyptr = rb_attr_get(wrapper, rb_intern("@__pyptr__"));
  if (NIL_P(pyptr) || !is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "Wrong wrapper object is given");
  }

  pyobj = get_pyobj_ptr(pyptr);

  if (RB_TYPE_P(name, T_SYMBOL)) {
    name = rb_sym_to_s(name);
  }

  name_cstr = StringValueCStr(name);
  if (name_cstr[RSTRING_LEN(name) - 1] == '=') {
    name_cstr[RSTRING_LEN(name) - 1] = '\0';
    attr = Py_API(PyObject_GetAttrString)(pyobj, name_cstr);
    name_cstr[RSTRING_LEN(name) - 1] = '=';
  }
  else {
    attr = Py_API(PyObject_GetAttrString)(pyobj, name_cstr);
  }
  if (!attr) {
    pycall_pyerror_fetch_and_raise("PyObject_GetAttrString in pycall_libpython_helpers_m_define_wrapper_method");
  }

  pycall_Py_DecRef(attr);
  rb_define_singleton_method(wrapper, name_cstr, pycall_pyobject_wrapper_wrapper_method, -1);

  return Qnil;
}

static PyObject *
pycall_convert_index(VALUE index)
{
  PyObject *pyobj;

  if (RB_TYPE_P(index, T_ARRAY) && RARRAY_LEN(index) == 1) {
    index = RARRAY_AREF(index, 0);
  }
  if (RB_TYPE_P(index, T_ARRAY)) {
    long i, n = RARRAY_LEN(index);
    pyobj = Py_API(PyTuple_New)(n);
    for (i = 0; i < n; ++i) {
      PyObject *pytem = pycall_convert_index(RARRAY_AREF(index, i)); /* New reference */
      Py_API(PyTuple_SetItem)(pyobj, i, pytem); /* Steal reference */
    }
  }
  else if (rb_obj_is_kind_of(index, rb_cRange)) {
    pyobj = pycall_pyslice_from_ruby(index); /* New reference */
  }
  else if (pycall_obj_is_step_range(index)) {
    pyobj = pycall_pyslice_from_ruby(index); /* New reference */
  }
  else {
    pyobj = pycall_pyobject_from_ruby(index); /* New reference */
  }

  return pyobj;
}

static VALUE
pycall_libpython_helpers_m_getitem(VALUE mod, VALUE pyptr, VALUE key)
{
  PyObject *pyobj, *pyobj_key, *pyobj_v;
  VALUE obj;

  if (!is_pycall_pyptr(pyptr)) {
    rb_raise(rb_eTypeError, "PyCall::PyPtr is required");
  }

  pyobj = get_pyobj_ptr(pyptr);

  pyobj_key = pycall_convert_index(key);

  pyobj_v = Py_API(PyObject_GetItem)(pyobj, pyobj_key);
  pycall_Py_DecRef(pyobj_key);
  if (!pyobj_v) {
    pycall_pyerror_fetch_and_raise("PyObject_GetItem in pycall_libpython_helpers_m_getitem");
  }

  obj = pycall_pyobject_to_ruby(pyobj_v);
  pycall_Py_DecRef(pyobj_v);
  return obj;
}

static VALUE
pycall_libpython_helpers_m_setitem(VALUE mod, VALUE pyptr, VALUE key, VALUE v)
{
  PyObject *pyobj, *pyobj_key, *pyobj_value;
  int res;

  pyobj = check_get_pyobj_ptr(pyptr, NULL);
  pyobj_key = pycall_convert_index(key);
  pyobj_value = pycall_pyobject_from_ruby(v);

  res = Py_API(PyObject_SetItem)(pyobj, pyobj_key, pyobj_value);
  pycall_Py_DecRef(pyobj_key);
  pycall_Py_DecRef(pyobj_value);
  if (res == -1) {
    pycall_pyerror_fetch_and_raise("PyObject_SetItem in pycall_libpython_helpers_m_setitem");
  }

  return v;
}

static VALUE
pycall_libpython_helpers_m_delitem(VALUE mod, VALUE pyptr, VALUE key)
{
  PyObject *pyobj, *pyobj_key;
  int res;

  pyobj = check_get_pyobj_ptr(pyptr, NULL);
  pyobj_key = pycall_convert_index(key);

  res = Py_API(PyObject_DelItem)(pyobj, pyobj_key);
  pycall_Py_DecRef(pyobj_key);
  if (res == -1) {
    pycall_pyerror_fetch_and_raise("PyObject_DelItem");
  }

  return Qnil;
}

static VALUE
pycall_libpython_helpers_m_str(VALUE mod, VALUE pyptr)
{
  PyObject *pyobj, *pyobj_str;

  pyobj = check_get_pyobj_ptr(pyptr, NULL);

  pyobj_str = Py_API(PyObject_Str)(pyobj);
  if (!pyobj_str) {
    pycall_pyerror_fetch_and_raise("PyObject_Str");
  }

  VALUE v = pycall_pyobject_to_ruby(pyobj_str);
  pycall_Py_DecRef(pyobj_str);
  return v;
}

static VALUE
pycall_libpython_helpers_m_dict_contains(VALUE mod, VALUE pyptr, VALUE key)
{
  PyObject *pyobj, *pyobj_key;
  int res;

  pyobj = check_get_pyobj_ptr(pyptr, Py_API(PyDict_Type));
  pyobj_key = pycall_pyobject_from_ruby(key);
  res = Py_API(PyDict_Contains)(pyobj, pyobj_key);
  pycall_Py_DecRef(pyobj_key);
  if (res == -1) {
    pycall_pyerror_fetch_and_raise("PyDict_Contains");
  }

  return res ? Qtrue : Qfalse;
}

static VALUE
pycall_libpython_helpers_m_dict_each(VALUE mod, VALUE pyptr)
{
  PyObject *pyobj, *pyobj_key, *pyobj_value;
  Py_ssize_t pos;

  pyobj = check_get_pyobj_ptr(pyptr, Py_API(PyDict_Type));

  pos = 0;
  while (Py_API(PyDict_Next)(pyobj, &pos, &pyobj_key, &pyobj_value)) {
    VALUE key, value;
    key = pycall_pyobject_to_ruby(pyobj_key);
    value = pycall_pyobject_to_ruby(pyobj_value);
    rb_yield(rb_assoc_new(key, value));
  }

  return Qnil;
}

static VALUE
pycall_libpython_helpers_m_sequence_contains(VALUE mod, VALUE pyptr, VALUE key)
{
  PyObject *pyobj, *pyobj_key;
  int res;

  pyobj = check_get_pyobj_ptr(pyptr, NULL);
  if (!Py_API(PySequence_Check)(pyobj))
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected a Python sequence object)", Py_TYPE(pyobj)->tp_name);

  pyobj_key = pycall_pyobject_from_ruby(key);
  res = Py_API(PySequence_Contains)(pyobj, pyobj_key);
  pycall_Py_DecRef(pyobj_key);
  if (res == -1) {
    pycall_pyerror_fetch_and_raise("PySequence_Contains");
  }

  return res ? Qtrue : Qfalse;
}

static VALUE
pycall_libpython_helpers_m_sequence_each(VALUE mod, VALUE pyptr)
{
  PyObject *pyobj, *pyobj_iter, *pyobj_item;

  pyobj = check_get_pyobj_ptr(pyptr, NULL);
  if (!Py_API(PySequence_Check)(pyobj))
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected a Python sequence object)", Py_TYPE(pyobj)->tp_name);

  pyobj_iter = Py_API(PyObject_GetIter)(pyobj);
  if (!pyobj_iter) {
    pycall_pyerror_fetch_and_raise("PyObject_GetIter in pycall_libpython_helpers_m_sequence_each");
  }

  while ((pyobj_item = Py_API(PyIter_Next)(pyobj_iter))) {
    rb_yield(pycall_pyobject_to_ruby(pyobj_item));
    pycall_Py_DecRef(pyobj_item);
  }

  pycall_Py_DecRef(pyobj_iter);

  if (Py_API(PyErr_Occurred)() != NULL) {
    pycall_pyerror_fetch_and_raise("checking error just in case at the end of pycall_libpython_helpers_m_sequence_each");
  }

  return Qnil;
}

/* ==== PyCall::PyObjectWrapper ==== */

static int
is_pyobject_wrapper(VALUE obj)
{
  return RTEST(rb_obj_is_kind_of(obj, mPyObjectWrapper));
}

static VALUE
pycall_pyobject_wrapper_get_pyptr(VALUE obj)
{
  if (!is_pyobject_wrapper(obj)) {
    rb_raise(rb_eTypeError, "PyCal::PyObjectWrapper is required");
  }

  return rb_funcall(obj, rb_intern("__pyptr__"), 0);
}

PyObject *
pycall_pyobject_wrapper_get_pyobj_ptr(VALUE obj)
{
  VALUE pyptr = pycall_pyobject_wrapper_get_pyptr(obj);
  return get_pyobj_ptr(pyptr);
}

static PyObject *
pycall_pyobject_wrapper_check_get_pyobj_ptr(VALUE obj, PyTypeObject *pytypeobj)
{
  PyObject *pyobj;

  pyobj = pycall_pyobject_wrapper_get_pyobj_ptr(obj);
  if (Py_TYPE(pyobj) != pytypeobj) {
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected %s)", Py_TYPE(pyobj)->tp_name, pytypeobj->tp_name);
  }

  return pyobj;
}

/* ==== PyCall::Conversion ==== */

static int
get_mapped_ancestor_class_iter(VALUE key, VALUE value, VALUE arg)
{
  VALUE *args = (VALUE *)arg;
  if (RTEST(pycall_pytypeptr_subclass_p(args[0], key))) {
    args[1] = value;
    return ST_STOP;
  }
  return ST_CONTINUE;
}

static VALUE
pycall_python_type_mapping_get_mapped_ancestor_class(VALUE pytypeptr)
{
  VALUE args[2];
  args[0] = pytypeptr;
  args[1] = Qnil;

  rb_hash_foreach(python_type_mapping, get_mapped_ancestor_class_iter, (VALUE)args);

  return args[1];
}

static VALUE
pycall_python_type_mapping_get_mapped_class(VALUE pytypeptr)
{
  VALUE mapped;
  (void)check_get_pytypeobj_ptr(pytypeptr);
  mapped = rb_hash_lookup(python_type_mapping, pytypeptr);
  if (NIL_P(mapped)) {
    mapped = pycall_python_type_mapping_get_mapped_ancestor_class(pytypeptr);
  }
  return mapped;
}

static int
pycall_python_type_mapping_register(VALUE pytypeptr, VALUE rbcls)
{
  (void)check_get_pytypeobj_ptr(pytypeptr);
  if (rb_hash_lookup2(python_type_mapping, pytypeptr, Qundef) != Qundef)
    return 0;

  Check_Type(rbcls, T_CLASS);
  if (!rb_obj_is_kind_of(rbcls, mPyTypeObjectWrapper)) {
    rb_raise(rb_eTypeError, "ruby class must be extended by PyCall::PyTypeObjectWrapper");
  }

  /* TODO: Shouldn't have to use weak reference? */
  rb_hash_aset(python_type_mapping, pytypeptr, rbcls);

  return 1;
}

static int
pycall_python_type_mapping_unregister(VALUE pytypeptr)
{
  (void)check_get_pytypeobj_ptr(pytypeptr);
  if (rb_hash_lookup2(python_type_mapping, pytypeptr, Qundef) == Qundef)
    return 0;

  rb_hash_delete(python_type_mapping, pytypeptr);
  return 1;
}

VALUE
pycall_pyobject_wrapper_object_new(VALUE klass, PyObject *pyobj)
{
  VALUE obj;

  obj = rb_obj_alloc(klass);
  rb_ivar_set(obj, rb_intern("@__pyptr__"), pycall_pyptr_new(pyobj));
  rb_extend_object(obj, mPyObjectWrapper);

  return obj;
}

VALUE
pycall_pyobject_to_ruby(PyObject *pyobj)
{
  VALUE cls;

  if (pyobj == Py_API(_Py_NoneStruct))
    return Qnil;

  if (PyRuby_Check(pyobj))
    return PyRuby_get_ruby_object(pyobj);

  if (PyType_Check(pyobj))
    return pycall_pytype_to_ruby(pyobj); /* Increment pyobj refcnt */

  if (PyClass_Check(pyobj))
    return pycall_pytype_to_ruby(pyobj); /* Increment pyobj refcnt */

  if (pyobj->ob_type == Py_API(PyModule_Type))
    return pycall_pymodule_to_ruby(pyobj); /* Increment pyobj refcnt */

  if (pyobj->ob_type == Py_API(PyBool_Type))
    return pycall_pybool_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyComplex_Type))
    return pycall_pycomplex_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyFloat_Type))
    return pycall_pyfloat_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyInt_Type))
    return pycall_pyint_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyLong_Type))
    return pycall_pylong_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyUnicode_Type))
    return pycall_pyunicode_to_ruby(pyobj);

  if (pyobj->ob_type == Py_API(PyString_Type))
    return pycall_pystring_to_ruby(pyobj);

  Py_API(Py_IncRef)(pyobj);
  Py_API(Py_IncRef)((PyObject *)pyobj->ob_type);
  cls = pycall_python_type_mapping_get_mapped_class(pycall_pytypeptr_new((PyObject *)pyobj->ob_type));
  if (NIL_P(cls)) {
    rb_warning("Currently do not support to convert %s to Ruby object", Py_TYPE(pyobj)->tp_name);
    return pycall_pyobject_wrapper_object_new(rb_cObject, pyobj);
  }

  return rb_funcall(cls, rb_intern("wrap_pyptr"), 1, pycall_pyptr_new(pyobj));
}

VALUE
pycall_pytype_to_ruby(PyObject *pyobj)
{
  VALUE pytypeptr = Qnil, wrapper_class;
  /* TODO: should look up wrapper class table instead of directly returning PyTypePtr */

  if (PyType_Check(pyobj))
    pytypeptr = pycall_pytypeptr_new(pyobj);
  else if (PyClass_Check(pyobj))
    pytypeptr = pycall_pytypeptr_new(pyobj);

  if (NIL_P(pytypeptr))
    return Qnil;

  Py_API(Py_IncRef)(pyobj);

  wrapper_class = rb_funcall(mPyCall, rb_intern("wrap_class"), 1, pytypeptr);
  return wrapper_class;
}

VALUE
pycall_pymodule_to_ruby(PyObject *pyobj)
{
  VALUE pymodptr = Qnil, wrapper_module;

  if (Py_TYPE(pyobj) != Py_API(PyModule_Type))
    return Qnil;

  pymodptr = pycall_pyptr_new(pyobj);
  Py_API(Py_IncRef)(pyobj);

  wrapper_module = rb_funcall(mPyCall, rb_intern("wrap_module"), 1, pymodptr);
  return wrapper_module;
}

VALUE
pycall_pybool_to_ruby(PyObject *pyobj)
{
  if (pyobj->ob_type != Py_API(PyBool_Type))
    return Qnil;

  if (Py_API(PyInt_Type))
    return Py_API(PyInt_AsSsize_t)(pyobj) ? Qtrue : Qfalse;

  return Py_API(PyLong_AsSsize_t)(pyobj) ? Qtrue : Qfalse;
}

VALUE
pycall_pycomplex_to_ruby(PyObject *pyobj)
{
  double real, imag;

  if (pyobj->ob_type != Py_API(PyComplex_Type))
    return Qnil;

  real = Py_API(PyComplex_RealAsDouble)(pyobj);
  imag = Py_API(PyComplex_ImagAsDouble)(pyobj);

  return rb_complex_new(DBL2NUM(real), DBL2NUM(imag));
}

VALUE
pycall_pyfloat_to_ruby(PyObject *pyobj)
{
  double d;

  if (pyobj->ob_type != Py_API(PyFloat_Type))
    return Qnil;

  {
    PyObject *type, *value, *tb;
    Py_API(PyErr_Fetch)(&type, &value, &tb);

    d = Py_API(PyFloat_AsDouble)(pyobj);
    if (d == -1.0) {
      if (Py_API(PyErr_Occurred)()) {
        pycall_pyerror_fetch_and_raise("PyFloat_AsDouble");
      }
    }

    Py_API(PyErr_Restore)(type, value, tb);
  }

  return DBL2NUM(d);
}

VALUE
pycall_pyint_to_ruby(PyObject *pyobj)
{
  Py_ssize_t n;

  if (pyobj->ob_type != Py_API(PyInt_Type))
    return Qnil;

  n = Py_API(PyInt_AsSsize_t)(pyobj);
  return SSIZET2NUM(n);
}

VALUE
pycall_pylong_to_ruby(PyObject *pyobj)
{
  int overflow;

  if (pyobj->ob_type != Py_API(PyLong_Type))
    return Qnil;

  {
    long n = Py_API(PyLong_AsLongAndOverflow)(pyobj, &overflow);
    if (overflow == 0) {
      if (Py_API(PyErr_Occurred)()) {
        pycall_pyerror_fetch_and_raise("PyLong_AsLongAndOverflow");
      }
      return LONG2FIX(n);
    }
  }

#if HAVE_LONG_LONG
  {
    LONG_LONG n = Py_API(PyLong_AsLongLongAndOverflow)(pyobj, &overflow);
    if (overflow == 0) {
      if (Py_API(PyErr_Occurred)()) {
        pycall_pyerror_fetch_and_raise("PyLong_AsLongLongAndOverflow");
      }
      return LL2NUM(n);
    }
  }
#endif

  rb_warning("Currently do not support to convert multi-precision PyLong number to Ruby object");

  return Qnil;
}

VALUE
pycall_pystring_to_ruby(PyObject *pyobj)
{
  char *str = NULL;
  Py_ssize_t len = 0;
  int res;

  /* TODO: PyString_Check */
  if (pyobj->ob_type != Py_API(PyString_Type)) {
    return Qnil;
  }

  res = Py_API(PyString_AsStringAndSize)(pyobj, &str, &len);
  if (res < 0) {
    return Qnil;
  }

  return rb_enc_str_new(str, len, rb_enc_from_index(rb_ascii8bit_encindex()));
}

VALUE
pycall_pyunicode_to_ruby(PyObject *pyobj)
{
  char *str = NULL;
  Py_ssize_t len = 0;
  int res;

  /* TODO: PyUnicode_Check */
  if (pyobj->ob_type != Py_API(PyUnicode_Type)) {
    return Qnil;
  }

  PyObject *pyobj_uni = Py_API(PyUnicode_AsUTF8String)(pyobj);
  if (!pyobj_uni) {
    Py_API(PyErr_Clear)();
    return Qnil;
  }

  res = Py_API(PyString_AsStringAndSize)(pyobj_uni, &str, &len);
  if (res < 0) {
    pycall_Py_DecRef(pyobj_uni);
    return Qnil;
  }

  VALUE v = rb_enc_str_new(str, len, rb_enc_from_index(rb_utf8_encindex()));
  pycall_Py_DecRef(pyobj_uni);
  return v;
}

static VALUE
pycall_pytuple_to_a(PyObject *pyobj)
{
  VALUE ary;
  Py_ssize_t i, n;

  assert(Py_TYPE(pyobj) == Py_API(PyTuple_Type));

  n = Py_API(PyTuple_Size)(pyobj);
  ary = rb_ary_new_capa(n);
  for (i = 0; i < n; ++i) {
    PyObject *pytem = Py_API(PyTuple_GetItem)(pyobj, i);
    Py_API(Py_IncRef)(pytem);
    rb_ary_push(ary, pycall_pyobject_to_ruby(pytem));
    pycall_Py_DecRef(pytem);
  }

  return ary;
}

static VALUE
pycall_pysequence_to_a(PyObject *pyobj)
{
  Py_ssize_t n, i;
  VALUE ary;

  assert(Py_API(PySequence_Check)(pyobj));

  n = Py_API(PySequence_Size)(pyobj);
  ary = rb_ary_new_capa(n);
  for (i = 0; i < n; ++i) {
    PyObject *pytem = Py_API(PySequence_GetItem)(pyobj, i);
    rb_ary_push(ary, pycall_pyobject_to_ruby(pytem));
    pycall_Py_DecRef(pytem);
  }

  return ary;
}

VALUE
pycall_pyobject_to_a(PyObject *pyobj)
{
  if (Py_TYPE(pyobj) == Py_API(PyTuple_Type)) {
    return pycall_pytuple_to_a(pyobj);
  }

  if (Py_API(PySequence_Check)(pyobj)) {
    return pycall_pysequence_to_a(pyobj);
  }

  /* TODO: PyDict_Type to assoc array */

  return rb_Array(pycall_pyobject_to_ruby(pyobj));
}

static VALUE
pycall_conv_m_register_python_type_mapping(VALUE mod, VALUE pytypeptr, VALUE rbcls)
{
  return pycall_python_type_mapping_register(pytypeptr, rbcls) ? Qtrue : Qfalse;
}

static VALUE
pycall_conv_m_unregister_python_type_mapping(VALUE mod, VALUE pytypeptr)
{
  return pycall_python_type_mapping_unregister(pytypeptr) ? Qtrue : Qfalse;
}

static VALUE
pycall_conv_m_from_ruby(VALUE mod, VALUE obj)
{
  PyObject *pyobj = pycall_pyobject_from_ruby(obj);
  if (PyType_Check(pyobj) || PyClass_Check(pyobj))
    return pycall_pytypeptr_new(pyobj);
  if (PyRuby_Check(pyobj))
    return pycall_pyrubyptr_new(pyobj);
  return pycall_pyptr_new(pyobj);
}

static VALUE
pycall_conv_m_to_ruby(VALUE mod, VALUE pyptr)
{
  VALUE obj, obj_pyptr;
  PyObject *pyobj = check_get_pyobj_ptr(pyptr, NULL);
  obj = obj_pyptr = pycall_pyobject_to_ruby(pyobj);
  if (is_pyobject_wrapper(obj)) {
    obj_pyptr = pycall_pyobject_wrapper_get_pyptr(obj);
  }
  if (is_pycall_pyptr(obj_pyptr) && obj_pyptr != pyptr && DATA_PTR(obj_pyptr) == pyobj) {
    Py_API(Py_IncRef)(pyobj);
  }
  return obj;
}

PyObject *
pycall_pyobject_from_ruby(VALUE obj)
{
  if (is_pycall_pyptr(obj)) {
    PyObject *pyobj = get_pyobj_ptr(obj);
    Py_API(Py_IncRef)(pyobj);
    return pyobj;
  }
  if (is_pyobject_wrapper(obj)) {
    PyObject *pyobj = pycall_pyobject_wrapper_get_pyobj_ptr(obj);
    Py_API(Py_IncRef)(pyobj);
    return pyobj;
  }
  if (obj == Qnil) {
    Py_API(Py_IncRef)(Py_API(_Py_NoneStruct));
    return Py_API(_Py_NoneStruct);
  }
  if (obj == Qtrue || obj == Qfalse) {
    return Py_API(PyBool_FromLong)(RTEST(obj));
  }
  if (RB_INTEGER_TYPE_P(obj)) {
    if (FIXNUM_P(obj)) {
      if (Py_API(PyInt_FromLong))
        return Py_API(PyInt_FromLong)(FIX2LONG(obj));
      return Py_API(PyLong_FromLong)(FIX2LONG(obj));
    }
#ifdef HAVE_LONG_LONG
    else {
      LONG_LONG ll;
      int state = 0;
      ll = (LONG_LONG)rb_protect((VALUE (*)(VALUE))rb_big2ll, obj, &state);
      if (!state) {
        if (Py_API(PyInt_FromSsize_t) && SSIZE_MIN <= ll && ll <= SSIZE_MAX)
          Py_API(PyInt_FromSsize_t)((Py_ssize_t)ll);
        return Py_API(PyLong_FromLongLong)(ll);
      }
    }
#endif
    rb_warn("Currently do not support to convert large integer values to PyLong");
    Py_API(Py_IncRef)(Py_API(_Py_NoneStruct));
    return Py_API(_Py_NoneStruct); /* FIXME */
  }
  if (RB_FLOAT_TYPE_P(obj)) {
    return Py_API(PyFloat_FromDouble)(RFLOAT_VALUE(obj));
  }
  if (RB_TYPE_P(obj, T_COMPLEX)) {
    VALUE re, im;
    re = rb_funcall(obj, rb_intern("real"), 0);
    im = rb_funcall(obj, rb_intern("imag"), 0);
    return Py_API(PyComplex_FromDoubles)(NUM2DBL(re), NUM2DBL(im));
  }
  if (RB_TYPE_P(obj, T_STRING) || RB_TYPE_P(obj, T_SYMBOL)) {
    return pycall_pystring_from_ruby(obj);
  }
  if (RB_TYPE_P(obj, T_ARRAY)) {
    return pycall_pylist_from_ruby(obj);
  }
  if (RB_TYPE_P(obj, T_HASH)) {
    return pycall_pydict_from_ruby(obj);
  }

  return PyRuby_New(obj);
}

PyObject *
pycall_pystring_from_ruby(VALUE obj)
{
  int is_binary, is_ascii_only;

  if (RB_TYPE_P(obj, T_SYMBOL)) {
    obj = rb_sym_to_s(obj);
  }

  StringValue(obj);

  is_binary = (rb_enc_get_index(obj) == rb_ascii8bit_encindex());
  is_ascii_only = (ENC_CODERANGE_7BIT == rb_enc_str_coderange(obj));

  if (is_binary || (!python_is_unicode_literals && is_ascii_only)) {
    return Py_API(PyString_FromStringAndSize)(RSTRING_PTR(obj), RSTRING_LEN(obj));
  }
  return Py_API(PyUnicode_DecodeUTF8)(RSTRING_PTR(obj), RSTRING_LEN(obj), NULL);
}

PyObject *
pycall_pytuple_from_ruby(VALUE obj)
{
  PyObject *pytupleobj;
  long i, n;

  obj = rb_convert_type(obj, T_ARRAY, "Array", "to_ary");
  n = RARRAY_LEN(obj);
  pytupleobj = Py_API(PyTuple_New)(n);
  if (!pytupleobj) {
    pycall_pyerror_fetch_and_raise("PyTuple_New in pycall_pytuple_from_ruby");
  }

  for (i = 0; i < n; ++i) {
    int res;
    PyObject *pytem;

    pytem = pycall_pyobject_from_ruby(RARRAY_AREF(obj, i));
    res = Py_API(PyTuple_SetItem)(pytupleobj, i, pytem);
    if (res == -1) {
      pycall_Py_DecRef(pytem);
      pycall_Py_DecRef(pytupleobj);
      pycall_pyerror_fetch_and_raise("PyTuple_SetItem");
    }
  }

  return pytupleobj;
}

PyObject *
pycall_pylist_from_ruby(VALUE obj)
{
  PyObject *pylistobj;
  long i, n;

  obj = rb_convert_type(obj, T_ARRAY, "Array", "to_ary");
  n = RARRAY_LEN(obj);
  pylistobj = Py_API(PyList_New)(n);
  if (!pylistobj) {
    pycall_pyerror_fetch_and_raise("PyList_New in pcall_pylist_from_ruby");
  }

  for (i = 0; i < n; ++i) {
    int res;
    PyObject *pytem;

    pytem = pycall_pyobject_from_ruby(RARRAY_AREF(obj, i));
    res = Py_API(PyList_SetItem)(pylistobj, i, pytem);

    if (res == -1) {
      pycall_Py_DecRef(pytem);
      pycall_Py_DecRef(pylistobj);
      pycall_pyerror_fetch_and_raise("PyList_SetItem in pycall_pylist_from_ruby");
    }
  }

  return pylistobj;
}

static int
pycall_pydict_from_ruby_iter(VALUE key, VALUE value, VALUE arg)
{
  PyObject *pydictobj = (PyObject *)arg;
  PyObject *pyobj_key, *pyobj_value;
  int res;

  pyobj_key = pycall_pyobject_from_ruby(key);
  pyobj_value = pycall_pyobject_from_ruby(value);
  res = Py_API(PyObject_SetItem)(pydictobj, pyobj_key, pyobj_value);
  pycall_Py_DecRef(pyobj_key);
  pycall_Py_DecRef(pyobj_value);

  return (res == -1) ? ST_STOP : ST_CONTINUE;
}

PyObject *
pycall_pydict_from_ruby(VALUE obj)
{
  PyObject *pydictobj;

  obj = rb_convert_type(obj, T_HASH, "Hash", "to_hash");
  pydictobj = Py_API(PyDict_New)();
  if (!pydictobj) {
    pycall_pyerror_fetch_and_raise("PyDict_New in pycall_pydict_from_ruby");
  }

  rb_hash_foreach(obj, pycall_pydict_from_ruby_iter, (VALUE)pydictobj);
  if (Py_API(PyErr_Occurred)()) {
    pycall_pyerror_fetch_and_raise("PyObject_SetItem in pycall_pydict_from_ruby_iter");
  }

  return pydictobj;
}

PyObject *
pycall_pyslice_from_ruby(VALUE obj)
{
  VALUE begin, end, step = Qnil;
  int exclude_end;
  PyObject *pystart, *pystop, *pystep, *pyslice;

  if (rb_obj_is_kind_of(obj, rb_cRange)) {
    pycall_extract_range(obj, &begin, &end, &exclude_end, NULL);
  }
  else if (pycall_obj_is_step_range(obj)) {
    pycall_extract_range(obj, &begin, &end, &exclude_end, &step);
  }
  else {
    rb_raise(rb_eTypeError, "unexpected argument type %s (expected Range or Enumerator generated by Range#step)", rb_class2name(CLASS_OF(obj)));
  }

  if (!NIL_P(step) && NUM2SSIZET(step) < 0) {
    if (!NIL_P(end)) {
      if (!exclude_end) {
        ssize_t end_i = NUM2SSIZET(end);
        switch (end_i) {
          case 0:
            end = Qnil;
            break;

          default:
            end = SSIZET2NUM(end_i - 1); /* TODO: limit check */
            break;
        }
      }
    }
  }
  else {
    if (!NIL_P(end)) {
      if (!exclude_end) {
        ssize_t end_i = NUM2SSIZET(end);
        switch (end_i) {
          case -1:
            end = Qnil;
            break;

          default:
            end = SSIZET2NUM(end_i + 1); /* TODO: limit check */
            break;
        }
      }
    }
  }

  pystart = pycall_pyobject_from_ruby(begin);
  pystop  = pycall_pyobject_from_ruby(end);
  pystep  = pycall_pyobject_from_ruby(step);

  pyslice = Py_API(PySlice_New)(pystart, pystop, pystep);
  /* PySlice_New increments reference counts of pystart, pystop, and pystep */
  pycall_Py_DecRef(pystart);
  pycall_Py_DecRef(pystop);
  pycall_Py_DecRef(pystep);

  return pyslice;
}

VALUE
pycall_pyerror_new(PyObject *type, PyObject *value, PyObject *traceback)
{
  VALUE init_args[3];

  init_args[0] = pycall_pyobject_to_ruby(type);
  init_args[1] = value ? pycall_pyobject_to_ruby(value) : Qnil;
  init_args[2] = traceback ? pycall_pyobject_to_ruby(traceback) : Qnil;

  return rb_class_new_instance(3, init_args, cPyError);
}

VALUE
pycall_pyerror_fetch(void)
{
  PyObject *type, *value, *traceback;

  if (Py_API(PyErr_Occurred)() == NULL)
    return Qnil;

  Py_API(PyErr_Fetch)(&type, &value, &traceback);
  if (type == NULL)
    return Qnil;

  return pycall_pyerror_new(type, value, traceback);
}

void
pycall_pyerror_fetch_and_raise(char const *format, ...)
{
  va_list args;
  VALUE pyerror, msg;

  RBIMPL_NONNULL_ARG(format);

  pyerror = pycall_pyerror_fetch();
  if (!NIL_P(pyerror))
    rb_exc_raise(pyerror);

  va_start(args, format);
  msg = rb_vsprintf(format, args);
  va_end(args);

  rb_exc_raise(rb_exc_new3(eError, msg));
}

unsigned long
pycall_default_tp_flags(void)
{
  unsigned long const stackless_extension_flag = python_has_stackless_extension ? Py_TPFLAGS_HAVE_STACKLESS_EXTENSION : 0;

  if (python_major_version >= 3) {
    return stackless_extension_flag | Py_TPFLAGS_HAVE_VERSION_TAG;
  }
  else {
    return stackless_extension_flag |
      Py_TPFLAGS_HAVE_GETCHARBUFFER |
      Py_TPFLAGS_HAVE_SEQUENCE_IN |
      Py_TPFLAGS_HAVE_INPLACEOPS |
      Py_TPFLAGS_HAVE_RICHCOMPARE |
      Py_TPFLAGS_HAVE_WEAKREFS |
      Py_TPFLAGS_HAVE_ITER |
      Py_TPFLAGS_HAVE_CLASS |
      Py_TPFLAGS_HAVE_INDEX
      ;
  }
}

PyObject *
pycall_pystring_from_format(char const *format, ...)
{
  va_list vargs;
  PyObject *res;

  va_start(vargs, format);
  res = pycall_pystring_from_formatv(format, vargs);
  va_end(vargs);

  return res;
}

PyObject *
pycall_pystring_from_formatv(char const *format, va_list vargs)
{
  if (python_is_unicode_literals)
    return Py_API(PyUnicode_FromFormatV)(format, vargs);
  else
    return Py_API(PyString_FromFormatV)(format, vargs);
}

/* ==== Python ==== */

int
pycall_PyObject_DelAttrString(PyObject *pyobj, const char *attr_name)
{
  /* PyObject_DelAttrString is defined by using PyObject_SetAttrString in CPython's abstract.h */
  return Py_API(PyObject_SetAttrString)(pyobj, attr_name, NULL);
}

static void
init_python(void)
{
  static char const *argv[1] = { "" };

  /* optional functions */
  if (! Py_API(PyObject_DelAttrString)) {
    /* The case of PyObject_DelAttrString as a macro */
    Py_API(PyObject_DelAttrString) = pycall_PyObject_DelAttrString;
  }

  Py_API(Py_InitializeEx)(0);
  Py_API(PySys_SetArgvEx)(0, (char **)argv, 0);

  if (!Py_API(PyEval_ThreadsInitialized)()) {
    Py_API(PyEval_InitThreads)();
  }

  /* check the availability of stackless extension */
  python_has_stackless_extension = (Py_API(PyImport_ImportModule)("stackless") != NULL);
  if (!python_has_stackless_extension) {
    Py_API(PyErr_Clear)();
  }

  /* builtins module */
  {
    char const *builtins_module_name = (python_major_version < 3) ? "__builtin__" : "builtins";
    python_builtins_module = Py_API(PyImport_ImportModule)(builtins_module_name);
  }

  /* sys module */

  {
    PyObject *sys, *hexversion;
    sys = Py_API(PyImport_ImportModule)("sys");

    /* hexversion */

    hexversion = Py_API(PyObject_GetAttrString)(sys, "hexversion");
    if (Py_API(PyInt_Type))
      python_hexversion = Py_API(PyInt_AsSsize_t)(hexversion);
    else
      python_hexversion = Py_API(PyLong_AsSsize_t)(hexversion);
  }

  /* constants */

  Py_API(Py_IncRef)(Py_API(_Py_NoneStruct));
  rb_define_const(mAPI, "None", pycall_pyptr_new(Py_API(_Py_NoneStruct)));

  Py_API(Py_IncRef)((PyObject *)Py_API(PyBool_Type));
  rb_define_const(mAPI, "PyBool_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyBool_Type)));
  if (Py_API(PyClass_Type)) {
    Py_API(Py_IncRef)((PyObject *)Py_API(PyClass_Type));
    rb_define_const(mAPI, "PyClass_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyClass_Type)));
  }
  Py_API(Py_IncRef)((PyObject *)Py_API(PyDict_Type));
  rb_define_const(mAPI, "PyDict_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyDict_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyFloat_Type));
  rb_define_const(mAPI, "PyFloat_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyFloat_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyInt_Type));
  rb_define_const(mAPI, "PyInt_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyInt_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyList_Type));
  rb_define_const(mAPI, "PyList_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyList_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyLong_Type));
  rb_define_const(mAPI, "PyLong_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyLong_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyModule_Type));
  rb_define_const(mAPI, "PyModule_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyModule_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyString_Type));
  rb_define_const(mAPI, "PyString_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyString_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyType_Type));
  rb_define_const(mAPI, "PyType_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyType_Type)));
  Py_API(Py_IncRef)((PyObject *)Py_API(PyUnicode_Type));
  rb_define_const(mAPI, "PyUnicode_Type", pycall_pytypeptr_new((PyObject *)Py_API(PyUnicode_Type)));

  pycall_init_exceptions(pycall_libpython_handle);
}

static VALUE
pycall_tuple_s_new(int argc, VALUE *argv, VALUE klass)
{
  VALUE obj;
  PyObject *pyobj;
  int i;

  pyobj = Py_API(PyTuple_New)(argc);
  for (i = 0; i < argc; ++i) {
    Py_API(PyTuple_SetItem)(pyobj, i, pycall_pyobject_from_ruby(argv[i]));

    /* NOTE: Although PyTuple_SetItem steals the item reference,
     * it is unnecessary to call Py_IncRef for the item because
     * pycall_pyobject_from_ruby increments the reference count
     * of its result. */
  }

  obj = pycall_pyobject_wrapper_object_new(klass, pyobj);
  return obj;
}

static VALUE
pycall_tuple_length(VALUE obj)
{
  PyObject *pyobj;
  Py_ssize_t n;

  pyobj = pycall_pyobject_wrapper_check_get_pyobj_ptr(obj, Py_API(PyTuple_Type));

  n = Py_API(PyTuple_Size)(pyobj);
  return SSIZET2NUM(n);
}

static VALUE
pycall_tuple_to_a(VALUE obj)
{
  PyObject *pyobj;

  pyobj = pycall_pyobject_wrapper_check_get_pyobj_ptr(obj, Py_API(PyTuple_Type));

  return pycall_pytuple_to_a(pyobj);
}

static VALUE
pycall_pyerror_s_occurred_p(VALUE klass)
{
  return Py_API(PyErr_Occurred)() != NULL ? Qtrue : Qfalse;
}

static VALUE
pycall_pyerror_s_fetch(VALUE klass)
{
  return pycall_pyerror_fetch();
}

static void
init_pyerror(void)
{
  rb_define_singleton_method(cPyError, "occurred?", pycall_pyerror_s_occurred_p, 0);
  rb_define_singleton_method(cPyError, "fetch", pycall_pyerror_s_fetch, 0);
}

static void
init_tuple(void)
{
  cTuple = rb_funcall(mPyCall, rb_intern("wrap_class"), 1, pycall_pytypeptr_new((PyObject *)Py_API(PyTuple_Type)));
  rb_define_const(mPyCall, "Tuple", cTuple);
  rb_funcall(cTuple, rb_intern("register_python_type_mapping"), 0);
  rb_define_singleton_method(cTuple, "new", pycall_tuple_s_new, -1);
  rb_define_method(cTuple, "length", pycall_tuple_length, 0);
  rb_define_method(cTuple, "to_a", pycall_tuple_to_a, 0);
  rb_define_alias(cTuple, "to_ary", "to_a");
}

void
Init_pycall(void)
{
  pycall_hash_salt = FIX2LONG(rb_hash(rb_str_new2("PyCall::PyObject")));

  mPyCall = rb_define_module("PyCall");
  mPyObjectWrapper = rb_const_get_at(mPyCall, rb_intern("PyObjectWrapper"));
  mPyTypeObjectWrapper = rb_const_get_at(mPyCall, rb_intern("PyTypeObjectWrapper"));
  mGCGuard = rb_define_module_under(mPyCall, "GCGuard");
  eError = rb_const_get_at(mPyCall, rb_intern("Error"));
  cPyError = rb_const_get_at(mPyCall, rb_intern("PyError"));

  /* PyCall */

  rb_define_module_function(mPyCall, "after_fork", pycall_after_fork, 0);

  pycall_tls_create((pycall_tls_key *)&without_gvl_key);
  rb_define_module_function(mPyCall, "without_gvl", pycall_m_without_gvl, 0);

  /* PyCall::PyPtr */

  cPyPtr = rb_define_class_under(mPyCall, "PyPtr", rb_cBasicObject);
  rb_define_singleton_method(cPyPtr, "incref", pycall_pyptr_s_incref, 1);
  rb_define_singleton_method(cPyPtr, "decref", pycall_pyptr_s_decref, 1);
  rb_define_singleton_method(cPyPtr, "sizeof", pycall_pyptr_s_sizeof, 1);

  rb_define_alloc_func(cPyPtr, pycall_pyptr_allocate);
  rb_define_method(cPyPtr, "initialize", pycall_pyptr_initialize, 1);
  rb_define_method(cPyPtr, "null?", pycall_pyptr_is_null, 0);
  rb_define_method(cPyPtr, "none?", pycall_pyptr_is_none, 0);
  rb_define_method(cPyPtr, "nil?", pycall_pyptr_is_nil, 0);
  rb_define_method(cPyPtr, "==", pycall_pyptr_eq, 1);
  rb_define_method(cPyPtr, "===", pycall_pyptr_eq, 1);
  rb_define_method(cPyPtr, "eql?", pycall_pyptr_eq, 1);
  rb_define_method(cPyPtr, "hash", pycall_pyptr_hash, 0);
  rb_define_method(cPyPtr, "__address__", pycall_pyptr_get_ptr_address, 0);
  rb_define_method(cPyPtr, "__ob_refcnt__", pycall_pyptr_get_ob_refcnt, 0);
  rb_define_method(cPyPtr, "__ob_type__", pycall_pyptr_get_ob_type, 0);
  rb_define_method(cPyPtr, "object_id", pycall_pyptr_object_id, 0);
  rb_define_method(cPyPtr, "class", pycall_pyptr_class, 0);
  rb_define_method(cPyPtr, "inspect", pycall_pyptr_inspect, 0);
  rb_define_method(cPyPtr, "kind_of?", pycall_pyptr_is_kind_of, 1);
  rb_define_method(cPyPtr, "is_a?", pycall_pyptr_is_kind_of, 1);

  rb_define_const(cPyPtr, "NULL", pycall_pyptr_new(NULL));

  /* PyCall::PyTypePtr */

  cPyTypePtr = rb_define_class_under(mPyCall, "PyTypePtr", cPyPtr);
  rb_define_alloc_func(cPyTypePtr, pycall_pytypeptr_allocate);
  rb_define_method(cPyTypePtr, "__ob_size__", pycall_pytypeptr_get_ob_size, 0);
  rb_define_method(cPyTypePtr, "__tp_name__", pycall_pytypeptr_get_tp_name, 0);
  rb_define_method(cPyTypePtr, "__tp_basicsize__", pycall_pytypeptr_get_tp_basicsize, 0);
  rb_define_method(cPyTypePtr, "__tp_flags__", pycall_pytypeptr_get_tp_flags, 0);
  rb_define_method(cPyTypePtr, "===", pycall_pytypeptr_eqq, 1);
  rb_define_method(cPyTypePtr, "<", pycall_pytypeptr_subclass_p, 1);

  /* PyCall::LibPython::API */

  mLibPython = rb_define_module_under(mPyCall, "LibPython");
  pycall_libpython_handle = rb_funcall(mLibPython, rb_intern("handle"), 0);
  pycall_init_libpython_api_table(pycall_libpython_handle);

  mAPI = rb_define_module_under(mLibPython, "API");

  rb_define_module_function(mAPI, "builtins_module_ptr", pycall_libpython_api_get_builtins_module_ptr, 0);

  rb_define_module_function(mAPI, "PyObject_Dir", pycall_libpython_api_PyObject_Dir, 1);
  rb_define_module_function(mAPI, "PyList_Size", pycall_libpython_api_PyList_Size, 1);
  rb_define_module_function(mAPI, "PyList_GetItem", pycall_libpython_api_PyList_GetItem, 2);

  /* PyCall::LibPython::Helpers */

  mHelpers = rb_define_module_under(mLibPython, "Helpers");

  rb_define_module_function(mHelpers, "unicode_literals?", pycall_libpython_helpers_m_unicode_literals_p, 0);
  rb_define_module_function(mHelpers, "import_module", pycall_libpython_helpers_m_import_module, -1);
  rb_define_module_function(mHelpers, "define_wrapper_method", pycall_libpython_helpers_m_define_wrapper_method, 2);
  rb_define_module_function(mHelpers, "compare", pycall_libpython_helpers_m_compare, 3);
  rb_define_module_function(mHelpers, "getattr", pycall_libpython_helpers_m_getattr, -1);
  rb_define_module_function(mHelpers, "hasattr?", pycall_libpython_helpers_m_hasattr_p, 2);
  rb_define_module_function(mHelpers, "setattr", pycall_libpython_helpers_m_setattr, 3);
  rb_define_module_function(mHelpers, "delattr", pycall_libpython_helpers_m_delattr, 2);
  rb_define_module_function(mHelpers, "callable?", pycall_libpython_helpers_m_callable_p, 1);
  rb_define_module_function(mHelpers, "call_object", pycall_libpython_helpers_m_call_object, -1);
  rb_define_module_function(mHelpers, "getitem", pycall_libpython_helpers_m_getitem, 2);
  rb_define_module_function(mHelpers, "setitem", pycall_libpython_helpers_m_setitem, 3);
  rb_define_module_function(mHelpers, "delitem", pycall_libpython_helpers_m_delitem, 2);
  rb_define_module_function(mHelpers, "str", pycall_libpython_helpers_m_str, 1);
  rb_define_module_function(mHelpers, "dict_contains", pycall_libpython_helpers_m_dict_contains, 2);
  rb_define_module_function(mHelpers, "dict_each", pycall_libpython_helpers_m_dict_each, 1);
  rb_define_module_function(mHelpers, "sequence_contains", pycall_libpython_helpers_m_sequence_contains, 2);
  rb_define_module_function(mHelpers, "sequence_each", pycall_libpython_helpers_m_sequence_each, 1);

  /* PyCall::Conversion */

  mConversion = rb_define_module_under(mPyCall, "Conversion");

  rb_define_module_function(mConversion, "register_python_type_mapping", pycall_conv_m_register_python_type_mapping, 2);
  rb_define_module_function(mConversion, "unregister_python_type_mapping", pycall_conv_m_unregister_python_type_mapping, 1);
  rb_define_module_function(mConversion, "from_ruby", pycall_conv_m_from_ruby, 1);
  rb_define_module_function(mConversion, "to_ruby", pycall_conv_m_to_ruby, 1);

  python_type_mapping = rb_hash_new();
  id_python_type_mapping = rb_intern("__python_type_mapping__");
  rb_ivar_set(mConversion, id_python_type_mapping, python_type_mapping);

  /* initialize the constat PYTHON_VERSION */

  python_description = rb_str_new2(Py_API(Py_GetVersion)());
  rb_define_const(mLibPython, "PYTHON_DESCRIPTION", python_description);

  {
    char *space_pos, *first_dot_pos;

    space_pos = memchr(RSTRING_PTR(python_description), ' ', RSTRING_LEN(python_description));
    python_version_string = rb_str_subseq(python_description, 0, space_pos - RSTRING_PTR(python_description));

    /* extract major version */
    first_dot_pos = memchr(RSTRING_PTR(python_version_string), '.', RSTRING_LEN(python_version_string));
    *first_dot_pos = '\0';
    python_major_version = (int)strtol(RSTRING_PTR(python_version_string), (char **)NULL, 10);
    *first_dot_pos = '.';
  }
  rb_define_const(mLibPython, "PYTHON_VERSION", python_version_string);

  /* initialize Python interpreter */

  init_python();
  init_pyerror();
  init_tuple();
  pycall_init_gcguard();
  pycall_init_ruby_wrapper();
}
