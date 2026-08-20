#include "pycall_internal.h"
#include "pycall.h"

static PyMemberDef PyRuby_members[] = {
  {"ruby_object_ptr", Py_T_PYSSIZET, offsetof(PyRubyObject, ruby_object), Py_READONLY},
  {NULL} /* sentinel */
};

static void PyRuby_dealloc_with_gvl(PyRubyObject *);
static PyObject * PyRuby_repr_with_gvl(PyRubyObject *);
static PyObject * PyRuby_call_with_gvl(PyRubyObject *, PyObject *, PyObject *);
static PyObject * PyRuby_getattro_with_gvl(PyRubyObject *, PyObject *);

PyTypeObject PyRuby_Type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  "PyCall.ruby_object", /* tp_name */
  sizeof(PyRubyObject), /* tp_basicsize */
  0, /* tp_itemsize */
  (destructor)PyRuby_dealloc_with_gvl, /* tp_dealloc */
  0,                             /* tp_print */
  0,                             /* tp_getattr */
  0,                             /* tp_setattr */
  0,                             /* tp_reserved */
  (reprfunc)PyRuby_repr_with_gvl, /* tp_repr */
  0,                             /* tp_as_number */
  0,                             /* tp_as_sequence */
  0,                             /* tp_as_mapping */
  {0},                           /* tp_hash  */
  (ternaryfunc)PyRuby_call_with_gvl, /* tp_call */
  0,                             /* tp_str */
  (getattrofunc)PyRuby_getattro_with_gvl, /* tp_getattro */
  0,                             /* tp_setattro */
  0,                             /* tp_as_buffer */
  Py_TPFLAGS_BASETYPE,           /* tp_flags */
  "ruby object wrapper",         /* tp_doc */
  0,                             /*tp_traverse*/
  0,                             /*tp_clear*/
  0,                             /*tp_richcompare*/
  0,                             /*tp_weaklistoffset*/
  0,                             /*tp_iter*/
  0,                             /*tp_iternext*/
  0,                             /*tp_methods*/
  PyRuby_members,                /*tp_members*/
};

static PyObject *
PyRuby_New_impl(VALUE ruby_object)
{
  PyRubyObject *op;

  op = (PyRubyObject *)Py_API(_PyObject_New)(&PyRuby_Type);
  op->ruby_object = ruby_object;
  pycall_gcguard_register_pyrubyobj((PyObject *)op);
  return (PyObject *)op;
}

PyObject *
PyRuby_New(VALUE ruby_object)
{
  if (!ruby_thread_has_gvl_p()) {
    return CALL_WITH_GVL(PyRuby_New_impl, ruby_object);
  }
  else {
    return PyRuby_New_impl(ruby_object);
  }
}

static void *
PyRuby_dealloc(PyRubyObject *pyro)
{
  VALUE obj;

  obj = PyRuby_get_ruby_object((PyObject *)pyro);

#ifdef PYCALL_DEBUG_DUMP_REFCNT
  fprintf(stderr, "PyRuby_dealloc(%p), ruby_object=%"PRI_LL_PREFIX"d\n", pyro, NUM2LL(rb_obj_id(obj)));
#endif /* PYCALL_DEBUG_DUMP_REFCNT */

  if (obj == Qundef)
    return NULL;

  pycall_gcguard_unregister_pyrubyobj((PyObject *)pyro);

  return NULL;
}

static void
PyRuby_dealloc_with_gvl(PyRubyObject *pyro)
{
  if (!ruby_thread_has_gvl_p()) {
    CALL_WITH_GVL(PyRuby_dealloc, pyro);
  }
  else {
    PyRuby_dealloc(pyro);
  }
}

static PyObject *
PyRuby_repr(PyRubyObject *pyro)
{
  VALUE obj, str;
  PyObject *res;

  obj = PyRuby_get_ruby_object((PyObject *)pyro);

  str = rb_inspect(obj);
  res = pycall_pystring_from_format("<PyCall.ruby_object %s>", StringValueCStr(str));
  return res;
}

static PyObject *
PyRuby_repr_with_gvl(PyRubyObject *pyro)
{
  if (!ruby_thread_has_gvl_p()) {
    return CALL_WITH_GVL(PyRuby_repr, pyro);
  }
  else {
    return PyRuby_repr(pyro);
  }
}

#if SIZEOF_SSIZE_T < 8
/* int64to32hash from src/support/hashing.c in Julia */
static inline uint32_t
int64to32hash(uint64_t key)
{
    key = (~key) + (key << 18); // key = (key << 18) - key - 1;
    key =   key  ^ (key >> 31);
    key = key * 21;             // key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 11);
    key = key + (key << 6);
    key = key ^ (key >> 22);
    return (uint32_t)key;
}
#endif

static void *
PyRuby_hash_long(PyRubyObject *pyro)
{
  VALUE obj, rbhash;
  intptr_t h;

  obj = PyRuby_get_ruby_object((PyObject *)pyro);

  rbhash = rb_hash(obj);
  h = FIX2LONG(rbhash); /* Ruby's hash value is a Fixnum */

  return (void *)(h == -1 ? pycall_hash_salt : h);
}

static long
PyRuby_hash_long_with_gvl(PyRubyObject *pyro)
{
  if (!ruby_thread_has_gvl_p()) {
    return (long)(intptr_t)CALL_WITH_GVL(PyRuby_hash_long, pyro);
  }
  else {
    return (long)(intptr_t)PyRuby_hash_long(pyro);
  }
}

static void *
PyRuby_hash_hash_t(PyRubyObject *pyro)
{
  VALUE obj, rbhash;
  Py_hash_t h;

  obj = PyRuby_get_ruby_object((PyObject *)pyro);

  rbhash = rb_hash(obj);
#if SIZEOF_PY_HASH_T == SIZEOF_LONG
  /* In this case, we can assume sizeof(Py_hash_t) == sizeof(long) */
  h = NUM2SSIZET(rbhash);
  return (void *)(h == -1 ? pycall_hash_salt : h);
#else
  /* In this case, we can assume sizeof(long) == 4 and sizeof(Py_hash_t) == 8 */
  h = ((Py_hash_t)pycall_hash_salt_32 << 32) | FIX2LONG(rbhash);
  return (void *)(h == -1 ? ((pycall_hash_salt << 32) | pycall_hash_salt) : h);
#endif
}

static Py_hash_t
PyRuby_hash_hash_t_with_gvl(PyRubyObject *pyro)
{
  if (!ruby_thread_has_gvl_p()) {
    return (Py_hash_t)CALL_WITH_GVL(PyRuby_hash_hash_t, pyro);
  }
  else {
    return (Py_hash_t)PyRuby_hash_hash_t(pyro);
  }
}

struct call_rb_funcallv_params {
  VALUE recv;
  ID meth;
  int argc;
  VALUE *argv;
};

static VALUE
call_rb_funcallv(struct call_rb_funcallv_params *params)
{
  return rb_funcallv(params->recv, params->meth, params->argc, params->argv);
}

static VALUE
rb_protect_funcallv(VALUE recv, ID meth, int argc, VALUE *argv, int *pstate)
{
  struct call_rb_funcallv_params params;
  VALUE res;
  int state;

  params.recv = recv;
  params.meth = meth;
  params.argc = argc;
  params.argv = argv;

  res = rb_protect((VALUE (*)(VALUE))call_rb_funcallv, (VALUE)&params, &state);
  if (pstate) *pstate = state;
  if (state) return Qnil;
  return res;
}

struct PyRuby_call_params {
  PyRubyObject *pyro;
  PyObject *pyobj_args;
  PyObject *pyobj_kwargs;
};

static PyObject *
PyRuby_call(struct PyRuby_call_params *params)
{
  ID id_call;
  VALUE obj, args, kwargs, res;
  PyObject *pyobj_res;
  int state;

  obj = PyRuby_get_ruby_object((PyObject *)params->pyro);

  id_call = rb_intern("call");
  if (!rb_respond_to(obj, id_call)) {
    Py_API(PyErr_SetString)(Py_API(PyExc_TypeError), "non-callable ruby object");
    return NULL;
  }

  args = pycall_pyobject_to_a(params->pyobj_args);
  if (params->pyobj_kwargs) {
    kwargs = pycall_pyobject_to_ruby(params->pyobj_kwargs);
    rb_ary_push(args, kwargs);
  }

  res = rb_protect_funcallv(obj, id_call, (int)RARRAY_LEN(args), RARRAY_PTR(args), &state);
  if (state) {
    /* TODO: pyerr set */
  }

  pyobj_res = pycall_pyobject_from_ruby(res);
  return pyobj_res;
}

static PyObject *
PyRuby_call_with_gvl(PyRubyObject *pyro, PyObject *pyobj_args, PyObject *pyobj_kwargs)
{
  struct PyRuby_call_params params;
  params.pyro = pyro;
  params.pyobj_args = pyobj_args;
  params.pyobj_kwargs = pyobj_kwargs;

  if (!ruby_thread_has_gvl_p()) {
    return CALL_WITH_GVL(PyRuby_call, &params);
  }
  else {
    return PyRuby_call(&params);
  }
}

struct PyRuby_getattro_params {
  PyRubyObject *pyro;
  PyObject *pyobj_name;
};

static PyObject *
PyRuby_getattro(struct PyRuby_getattro_params *params)
{
  VALUE obj, name, res;
  char const *name_cstr;
  ID name_id;
  PyObject *pyobj_res;

  obj = PyRuby_get_ruby_object((PyObject *)params->pyro);

  name = pycall_pyobject_to_ruby(params->pyobj_name);
  name_cstr = StringValueCStr(name);
  name_id = rb_intern(name_cstr);

  /* TODO: should handle exception */
  if (strncmp(name_cstr, "__name__", 8) == 0 ||
      strncmp(name_cstr, "func_name", 9) == 0) {
    if (rb_respond_to(obj, rb_intern("name"))) {
      res = rb_funcall(obj, rb_intern("name"), 0);
    }
    else {
      res = rb_any_to_s(obj);
    }
  }
  else if (strncmp(name_cstr, "__doc__", 7) == 0 ||
           strncmp(name_cstr, "func_doc", 8) == 0) {
    /* TODO: support docstring */
    res = Qnil;
  }
  else if (strncmp(name_cstr, "__module__", 10) == 0) {
    res = Qnil;
  }
  else if (strncmp(name_cstr, "__defaults__", 12) == 0 ||
           strncmp(name_cstr, "func_defaults", 13) == 0) {
    res = Qnil;
  }
  else if (strncmp(name_cstr, "__closure__", 11) == 0 ||
           strncmp(name_cstr, "func_closure", 12) == 0) {
    res = Qnil;
  }
  else if (name_cstr[0] == '_' && name_cstr[1] == '_') {
    /* name.start_with? "__" */
    /* TODO: handle `__code__` and `func_code` */
    return Py_API(PyObject_GenericGetAttr)((PyObject *)params->pyro, params->pyobj_name);
  }
  else {
    /* TODO: handle `__code__` and `func_code` */
    if (rb_respond_to(obj, name_id)) {
      VALUE method = rb_obj_method(obj, name);
      return PyRuby_New_impl(method);
    }
    return Py_API(PyObject_GenericGetAttr)((PyObject *)params->pyro, params->pyobj_name);
  }

  pyobj_res = pycall_pyobject_from_ruby(res);
  return pyobj_res;
}

static PyObject *
PyRuby_getattro_with_gvl(PyRubyObject *pyro, PyObject *pyobj_name)
{
  struct PyRuby_getattro_params params;
  params.pyro = pyro;
  params.pyobj_name = pyobj_name;

  if (!ruby_thread_has_gvl_p()) {
    return CALL_WITH_GVL(PyRuby_getattro, &params);
  }
  else {
    return PyRuby_getattro(&params);
  }
}

/* ==== PyCall::PyRubyPtr ==== */

VALUE cPyRubyPtr;

static rb_data_type_t pycall_pyrubyptr_data_type = {
  "PyCall::PyRubyPtr",
  { 0, pycall_pyptr_free, pycall_pyptr_memsize, },
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
# if defined  _WIN32 && !defined __CYGWIN__
  0,
# else
  &pycall_pyptr_data_type,
# endif
  0, RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

static inline int
is_pycall_pyrubyptr(VALUE obj)
{
  return rb_typeddata_is_kind_of(obj, &pycall_pyrubyptr_data_type);
}

static inline PyRubyObject*
get_pyrubyobj_ptr(VALUE obj)
{
  PyRubyObject *pyruby;
  TypedData_Get_Struct(obj, PyRubyObject, &pycall_pyrubyptr_data_type, pyruby);
  return pyruby;
}

static inline PyRubyObject*
try_get_pyrubyobj_ptr(VALUE obj)
{
  if (is_pycall_pyrubyptr(obj)) return NULL;
  return (PyRubyObject*)DATA_PTR(obj);
}

static inline PyRubyObject *
check_get_pyrubyobj_ptr(VALUE obj)
{
  PyRubyObject *pyrubyobj;
  if (!is_pycall_pyrubyptr(obj))
    rb_raise(rb_eTypeError, "unexpected type %s (expected PyCall::PyRubyPtr)", rb_class2name(CLASS_OF(obj)));

  pyrubyobj = get_pyrubyobj_ptr(obj);
  if (!PyRuby_Check(pyrubyobj))
    rb_raise(rb_eTypeError, "unexpected Python type %s (expected ruby object)", Py_TYPE(pyrubyobj)->tp_name);

  return pyrubyobj;
}

static VALUE
pycall_pyruby_allocate(VALUE klass)
{
  return TypedData_Wrap_Struct(klass, &pycall_pyrubyptr_data_type, NULL);
}

VALUE
pycall_pyrubyptr_new(PyObject *pyrubyobj)
{
  VALUE obj;

  if (!PyRuby_Check(pyrubyobj)) {
    rb_raise(rb_eTypeError, "wrong type of python object %s (expect PyRubyObject)", Py_TYPE(pyrubyobj)->tp_name);
  }

  obj = pycall_pyruby_allocate(cPyRubyPtr);
  DATA_PTR(obj) = pyrubyobj;
  return obj;
}

static VALUE
pycall_pyruby_get_ruby_object_id(VALUE obj)
{
  PyRubyObject *pyrubyobj;

  pyrubyobj = check_get_pyrubyobj_ptr(obj);
  return rb_obj_id(pyrubyobj->ruby_object);
}

VALUE
pycall_wrap_ruby_object(VALUE obj)
{
  PyObject *pyobj;
  pyobj = PyRuby_New(obj);
  return pycall_pyrubyptr_new(pyobj);
}

static VALUE
pycall_m_wrap_ruby_object(VALUE mod, VALUE obj)
{
  return pycall_wrap_ruby_object(obj);
}

void
pycall_init_ruby_wrapper(void)
{
  PyRuby_Type.ob_base.ob_type = Py_API(PyType_Type);
  PyRuby_Type.tp_flags |= pycall_default_tp_flags();
  PyRuby_Type.tp_new = Py_API(PyType_GenericNew);
  if (pycall_python_long_hash)
    PyRuby_Type.tp_hash._long = (hashfunc_long)PyRuby_hash_long_with_gvl;
  else
    PyRuby_Type.tp_hash._hash_t = (hashfunc_hash_t)PyRuby_hash_hash_t_with_gvl;

  if (Py_API(PyType_Ready)(&PyRuby_Type) < 0) {
    pycall_pyerror_fetch_and_raise("PyType_Ready in pycall_init_ruby_wrapper");
  }
  Py_API(Py_IncRef)((PyObject *)&PyRuby_Type);

  /* TODO */

  /* PyCall::PyRubyPtr */

#if defined _WIN32 && !defined __CYGWIN__
  pycall_pyrubyptr_data_type.parent = &pycall_pyptr_data_type;
#endif

  cPyRubyPtr = rb_define_class_under(mPyCall, "PyRubyPtr", cPyPtr);
  rb_define_alloc_func(cPyRubyPtr, pycall_pyruby_allocate);
  rb_define_method(cPyRubyPtr, "__ruby_object_id__", pycall_pyruby_get_ruby_object_id, 0);

  rb_define_module_function(mPyCall, "wrap_ruby_object", pycall_m_wrap_ruby_object, 1);
}

