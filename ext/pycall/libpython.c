#include "pycall_internal.h"

static pycall_libpython_api_table_t api_table = { NULL, };
static int python_string_as_bytes;

pycall_libpython_api_table_t *
pycall_libpython_api_table(void)
{
  return &api_table;
}

struct lookup_libpython_api_args {
  VALUE libpython_handle;
  char const *name;
};

static VALUE
lookup_libpython_api_0(struct lookup_libpython_api_args *args)
{
  return rb_funcall(args->libpython_handle, rb_intern("sym"), 1, rb_str_new2(args->name));
}

static void *
lookup_libpython_api(VALUE libpython_handle, char const *name)
{
  struct lookup_libpython_api_args arg;
  VALUE addr;
  int state;

  arg.libpython_handle = libpython_handle;
  arg.name = name;
  addr = rb_protect((VALUE (*)(VALUE))lookup_libpython_api_0, (VALUE)&arg, &state);
  if (state) {
    rb_set_errinfo(Qnil);
    return NULL;
  }
  else {
    return NIL_P(addr) ? NULL : NUM2PTR(addr);
  }
}

#define LOOKUP_API_ENTRY(api_name) lookup_libpython_api(libpython_handle, #api_name)
#define CHECK_API_ENTRY(api_name) (LOOKUP_API_ENTRY(api_name) != NULL)

#define required 1
#define optional 0

#define INIT_API_TABLE_ENTRY2(member_name, api_name, required) do { \
  void *fptr = LOOKUP_API_ENTRY(api_name); \
  if (!fptr && required) { \
    rb_raise(eLibPythonFunctionNotFound, "Unable to find the required symbol in libpython: %s", #api_name); \
  } \
  (api_table.member_name) = fptr; \
} while (0)

#define INIT_API_TABLE_ENTRY(api_name, required) INIT_API_TABLE_ENTRY2(api_name, api_name, required)

#define INIT_API_TABLE_ENTRY_PTR(api_name, required) do { \
  INIT_API_TABLE_ENTRY(api_name, required); \
  (api_table.api_name) = *(void **)(api_table.api_name); \
} while (0)

void
pycall_init_libpython_api_table(VALUE libpython_handle)
{
  VALUE eLibPythonFunctionNotFound = rb_const_get_at(pycall_mPyCall, rb_intern("LibPythonFunctionNotFound"));

  INIT_API_TABLE_ENTRY(_Py_NoneStruct, required);

  INIT_API_TABLE_ENTRY(PyBool_Type, required);
  INIT_API_TABLE_ENTRY(PyClass_Type, optional);
  INIT_API_TABLE_ENTRY(PyComplex_Type, required);
  INIT_API_TABLE_ENTRY(PyDict_Type, required);
  INIT_API_TABLE_ENTRY(PyFloat_Type, required);
  INIT_API_TABLE_ENTRY(PyList_Type, required);
  INIT_API_TABLE_ENTRY(PyInstance_Type, optional);
  INIT_API_TABLE_ENTRY(PyInt_Type, optional);
  INIT_API_TABLE_ENTRY(PyLong_Type, required);
  INIT_API_TABLE_ENTRY(PyModule_Type, required);
  python_string_as_bytes = !CHECK_API_ENTRY(PyString_Type);
  if (python_string_as_bytes) {
    INIT_API_TABLE_ENTRY2(PyString_Type, PyBytes_Type, required);
  }
  else {
    INIT_API_TABLE_ENTRY(PyString_Type, required);
  }
  INIT_API_TABLE_ENTRY(PyTuple_Type, required);
  INIT_API_TABLE_ENTRY(PyType_Type, required);
  INIT_API_TABLE_ENTRY(PyUnicode_Type, required);

  INIT_API_TABLE_ENTRY(Py_InitializeEx, required);
  INIT_API_TABLE_ENTRY(Py_FinalizeEx, required);
  INIT_API_TABLE_ENTRY(Py_IsInitialized, required);
  INIT_API_TABLE_ENTRY(Py_GetVersion, required);

  INIT_API_TABLE_ENTRY(PySys_SetArgvEx, required);

  INIT_API_TABLE_ENTRY(Py_IncRef, required);
  INIT_API_TABLE_ENTRY(Py_DecRef, required);

  INIT_API_TABLE_ENTRY(_PyObject_New, required);
  INIT_API_TABLE_ENTRY(PyCallable_Check, required);
  INIT_API_TABLE_ENTRY(PyObject_IsInstance, required);
  INIT_API_TABLE_ENTRY(PyObject_IsSubclass, required);
  INIT_API_TABLE_ENTRY2(PyObject_Hash._hash_t, PyObject_Hash, required);
  INIT_API_TABLE_ENTRY(PyObject_RichCompare, required);
  INIT_API_TABLE_ENTRY(PyObject_Call, required);
  INIT_API_TABLE_ENTRY(PyObject_CallMethod, required);
  INIT_API_TABLE_ENTRY(PyObject_Dir, required);
  INIT_API_TABLE_ENTRY(PyObject_GenericGetAttr, required);
  INIT_API_TABLE_ENTRY(PyObject_GetAttrString, required);
  INIT_API_TABLE_ENTRY(PyObject_SetAttrString, required);
  INIT_API_TABLE_ENTRY(PyObject_HasAttrString, required);
  INIT_API_TABLE_ENTRY(PyObject_DelAttrString, optional);
  INIT_API_TABLE_ENTRY(PyObject_GetItem, required);
  INIT_API_TABLE_ENTRY(PyObject_SetItem, required);
  INIT_API_TABLE_ENTRY(PyObject_DelItem, required);
  INIT_API_TABLE_ENTRY(PyObject_GetIter, required);
  INIT_API_TABLE_ENTRY(PyObject_Str, required);
  INIT_API_TABLE_ENTRY(PyObject_Repr, required);

  INIT_API_TABLE_ENTRY(PyType_Ready, required);
  INIT_API_TABLE_ENTRY(PyType_GenericNew, required);

  INIT_API_TABLE_ENTRY(PyCFunction_NewEx, required);

  INIT_API_TABLE_ENTRY(PyWeakref_NewRef, required);

  INIT_API_TABLE_ENTRY(PyBool_FromLong, required);

  INIT_API_TABLE_ENTRY(PyComplex_RealAsDouble, required);
  INIT_API_TABLE_ENTRY(PyComplex_ImagAsDouble, required);
  INIT_API_TABLE_ENTRY(PyComplex_FromDoubles, required);

  INIT_API_TABLE_ENTRY(PyFloat_AsDouble, required);
  INIT_API_TABLE_ENTRY(PyFloat_FromDouble, required);

  INIT_API_TABLE_ENTRY(PyList_New, required);
  INIT_API_TABLE_ENTRY(PyList_Size, required);
  INIT_API_TABLE_ENTRY(PyList_GetItem, required);
  INIT_API_TABLE_ENTRY(PyList_SetItem, required);
  INIT_API_TABLE_ENTRY(PyList_Insert, required);
  INIT_API_TABLE_ENTRY(PyList_Append, required);

  INIT_API_TABLE_ENTRY(PyInt_FromLong, optional);
  INIT_API_TABLE_ENTRY(PyInt_FromSsize_t, optional);
  INIT_API_TABLE_ENTRY(PyInt_AsSsize_t, optional);

  INIT_API_TABLE_ENTRY(PyLong_AsLongAndOverflow, required);
  INIT_API_TABLE_ENTRY(PyLong_FromLong, required);
#ifdef HAVE_LONG_LONG
  INIT_API_TABLE_ENTRY(PyLong_AsLongLongAndOverflow, required);
  INIT_API_TABLE_ENTRY(PyLong_FromLongLong, required);
#endif
  INIT_API_TABLE_ENTRY(PyLong_AsSsize_t, required);

  INIT_API_TABLE_ENTRY(PyTuple_New, required);
  INIT_API_TABLE_ENTRY(PyTuple_Size, required);
  INIT_API_TABLE_ENTRY(PyTuple_GetItem, required);
  INIT_API_TABLE_ENTRY(PyTuple_SetItem, required);

  INIT_API_TABLE_ENTRY(PySlice_New, required);

  INIT_API_TABLE_ENTRY(PyIter_Next, required);

  INIT_API_TABLE_ENTRY(PyEval_ThreadsInitialized, required);
  INIT_API_TABLE_ENTRY(PyEval_InitThreads, required);

  INIT_API_TABLE_ENTRY(PyErr_Occurred, required);
  INIT_API_TABLE_ENTRY(PyErr_Fetch, required);
  INIT_API_TABLE_ENTRY(PyErr_Restore, required);
  INIT_API_TABLE_ENTRY(PyErr_Clear, required);
  INIT_API_TABLE_ENTRY(PyErr_SetString, required);
  INIT_API_TABLE_ENTRY(PyErr_Format, required);
  INIT_API_TABLE_ENTRY(PyErr_SetInterrupt, required);

  INIT_API_TABLE_ENTRY(PyImport_ImportModule, required);
  INIT_API_TABLE_ENTRY(PyImport_ImportModuleLevel, required);

  INIT_API_TABLE_ENTRY(PyOS_AfterFork, required);

  INIT_API_TABLE_ENTRY(PyList_Size, required);
  INIT_API_TABLE_ENTRY(PyList_GetItem, required);

  INIT_API_TABLE_ENTRY(PyDict_New, required);
  INIT_API_TABLE_ENTRY(PyDict_Contains, required);
  INIT_API_TABLE_ENTRY(PyDict_SetItemString, required);
  INIT_API_TABLE_ENTRY(PyDict_Next, required);

  INIT_API_TABLE_ENTRY(PySequence_Check, required);
  INIT_API_TABLE_ENTRY(PySequence_Size, required);
  INIT_API_TABLE_ENTRY(PySequence_Contains, required);
  INIT_API_TABLE_ENTRY(PySequence_GetItem, required);

  if (python_string_as_bytes) {
    INIT_API_TABLE_ENTRY2(PyString_AsStringAndSize, PyBytes_AsStringAndSize, required);
    INIT_API_TABLE_ENTRY2(PyString_FromStringAndSize, PyBytes_FromStringAndSize, required);
    INIT_API_TABLE_ENTRY2(PyString_FromFormatV, PyBytes_FromFormat, required);
  }
  else {
    INIT_API_TABLE_ENTRY(PyString_AsStringAndSize, required);
    INIT_API_TABLE_ENTRY(PyString_FromStringAndSize, required);
    INIT_API_TABLE_ENTRY(PyString_FromFormatV, required);
  }

  if (CHECK_API_ENTRY(PyUnicode_DecodeUTF8)) {
    INIT_API_TABLE_ENTRY(PyUnicode_AsUTF8String, required);
    INIT_API_TABLE_ENTRY(PyUnicode_DecodeUTF8, required);
    INIT_API_TABLE_ENTRY(PyUnicode_FromFormatV, required);
  }
  else if (CHECK_API_ENTRY(PyUnicodeUCS4_DecodeUTF8)) {
    INIT_API_TABLE_ENTRY2(PyUnicode_AsUTF8String, PyUnicodeUCS4_AsUTF8String, required);
    INIT_API_TABLE_ENTRY2(PyUnicode_DecodeUTF8, PyUnicodeUCS4_DecodeUTF8, required);
    INIT_API_TABLE_ENTRY2(PyUnicode_FromFormatV, PyUnicodeUCS4_FromFormatV, required);
  }
  else if (CHECK_API_ENTRY(PyUnicodeUCS2_DecodeUTF8)) {
    INIT_API_TABLE_ENTRY2(PyUnicode_AsUTF8String, PyUnicodeUCS2_AsUTF8String, required);
    INIT_API_TABLE_ENTRY2(PyUnicode_DecodeUTF8, PyUnicodeUCS2_DecodeUTF8, required);
    INIT_API_TABLE_ENTRY2(PyUnicode_FromFormatV, PyUnicodeUCS2_FromFormatV, required);
  }

  INIT_API_TABLE_ENTRY(PyGILState_Check, required);
  INIT_API_TABLE_ENTRY(PyGILState_Ensure, required);
  INIT_API_TABLE_ENTRY(PyGILState_Release, required);
}

void
pycall_init_exceptions(VALUE libpython_handle)
{
  VALUE eLibPythonFunctionNotFound = rb_const_get_at(pycall_mPyCall, rb_intern("LibPythonFunctionNotFound"));

  INIT_API_TABLE_ENTRY_PTR(PyExc_RuntimeError, required);
  INIT_API_TABLE_ENTRY_PTR(PyExc_TypeError, required);
}
