#ifndef PYCALL_INTERNAL_H
#define PYCALL_INTERNAL_H 1

#if defined(__cplusplus)
extern "C" {
#if 0
} /* satisfy cc-mode */
#endif
#endif

#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>

#if defined(_WIN32)
# define PYCALL_THREAD_WIN32
# include <ruby/win32.h>
#elif defined(HAVE_PTHREAD_H)
# define PYCALL_THREAD_PTHREAD
# include <pthread.h>
#endif

#if SIZEOF_LONG == SIZEOF_VOIDP
# define PTR2NUM(x)   (LONG2NUM((long)(x)))
# define NUM2PTR(x)   ((void*)(NUM2ULONG(x)))
#elif SIZEOF_LONG_LONG == SIZEOF_VOIDP
# define PTR2NUM(x)   (LL2NUM((LONG_LONG)(x)))
# define NUM2PTR(x)   ((void*)(NUM2ULL(x)))
#else
# error ---->> ruby requires sizeof(void*) == sizeof(long) or sizeof(LONG_LONG) to be compiled. <<----
#endif

#ifndef RUBY_ASSERT
# define RUBY_ASSERT(expr) assert(expr)
#endif

#ifndef RBIMPL_ATTR_NONNULL
# define RBIMPL_ATTR_NONNULL(list) /* void */
#endif

#ifndef RBIMPL_NONNULL_ARG
# define RBIMPL_NONNULL_ARG(arg) RUBY_ASSERT(arg)
#endif

#ifndef RBIMPL_ATTR_FORMAT
# define RBIMPL_ATTR_FORMAT(x, y, z) /* void */
#endif

#ifndef RB_INTEGER_TYPE_P
# define RB_INTEGER_TYPE_P(obj) pycall_integer_type_p(obj)
static inline int
pycall_integer_type_p(VALUE obj)
{
  return (FIXNUM_P(obj) || (!SPECIAL_CONST_P(obj) && BUILTIN_TYPE(obj) == RUBY_T_BIGNUM));
}
#endif

void ruby_debug_breakpoint();
int ruby_thread_has_gvl_p(void);

#define CALL_WITH_GVL(func, data) rb_thread_call_with_gvl((void * (*)(void *))(func), (void *)(data))

/* ==== python ==== */

typedef intptr_t Py_intptr_t;

#ifdef HAVE_SSIZE_T
typedef ssize_t Py_ssize_t;
#elif SIZEOF_SIZE_T == SIZEOF_VOIDP
typedef Py_intptr_t Py_ssize_t;
#elif SIZEOF_SIZE_T == SIZEOF_INT
typedef int Py_ssize_t;
#elif SIZEOF_SIZE_T == SIZEOF_LONG
typedef long Py_ssize_t;
#elif defined(HAVE_LONG_LONG) && SIZEOF_SIZE_T == SIZEOF_LONG_LONG
typedef LONG_LONG Py_ssize_t;
#else
# error "Unable to typedef Py_ssize_t"
#endif

#undef SSIZE_MIN
#undef SSIZE_MAX

#if SIZEOF_SIZE_T == SIZEOF_INT
# define SSIZE_MIN INT_MIN
# define SSIZE_MAX INT_MAX
#elif SIZEOF_SIZE_T == SIZEOF_LONG
# define SSIZE_MIN LONG_MIN
# define SSIZE_MAX LONG_MAX
#elif defined(HAVE_LONG_LONG) && SIZEOF_SIZE_T == SIZEOF_LONG_LONG
# define SSIZE_MIN LLONG_MIN
# define SSIZE_MAX LLONG_MAX
#endif

#define SIZEOF_PY_HASH_T SIZEOF_SSIZE_T
typedef Py_ssize_t Py_hash_t;

/* NOTE: Currently Py_TRACE_REFS is not supported */
#define _PyObject_HEAD_EXTRA
#define _PyObject_EXTRA_INIT

#define PyObject_HEAD    \
  _PyObject_HEAD_EXTRA   \
  Py_ssize_t ob_refcnt;  \
  struct _typeobject *ob_type;

#define PyObject_VAR_HEAD  \
  PyObject ob_base;        \
  Py_ssize_t ob_size; /* Number of items in variable part */

#define PyObject_HEAD_INIT(type)  \
  _PyObject_EXTRA_INIT            \
  { 1, type },

#define PyVarObject_HEAD_INIT(type, size)  \
  PyObject_HEAD_INIT(type) size,

#define Py_INVALID_SIZE ((Py_ssize_t)-1)

typedef struct {
  PyObject_HEAD
} PyObject;

typedef struct {
  PyObject_VAR_HEAD
} PyVarObject;

#define Py_REFCNT(ob)           (((PyObject*)(ob))->ob_refcnt)
#define Py_TYPE(ob)             (((PyObject*)(ob))->ob_type)
#define Py_SIZE(ob)             (((PyVarObject*)(ob))->ob_size)

typedef PyObject * (*unaryfunc)(PyObject *);
typedef PyObject * (*binaryfunc)(PyObject *, PyObject *);
typedef PyObject * (*ternaryfunc)(PyObject *, PyObject *, PyObject *);
typedef int (*inquiry)(PyObject *);
typedef Py_ssize_t (*lenfunc)(PyObject *);
typedef PyObject *(*ssizeargfunc)(PyObject *, Py_ssize_t);
typedef int(*ssizeobjargproc)(PyObject *, Py_ssize_t, PyObject *);
typedef int(*objobjargproc)(PyObject *, PyObject *, PyObject *);

typedef struct bufferinfo {
  void *buf;
  PyObject *obj;        /* owned reference */
  Py_ssize_t len;
  Py_ssize_t itemsize;  /* This is Py_ssize_t so it can be
			   pointed to by strides in simple case.*/
  int readonly;
  int ndim;
  char *format;
  Py_ssize_t *shape;
  Py_ssize_t *strides;
  Py_ssize_t *suboffsets;
  void *internal;
} Py_buffer;

typedef int (*getbufferproc)(PyObject *, Py_buffer *, int);
typedef void (*releasebufferproc)(PyObject *, Py_buffer *);

typedef int (*objobjproc)(PyObject *, PyObject *);
typedef int (*visitproc)(PyObject *, void *);
typedef int (*traverseproc)(PyObject *, visitproc, void *);

typedef struct {
    /* Number implementations must check *both*
       arguments for proper type and implement the necessary conversions
       in the slot functions themselves. */

    binaryfunc nb_add;
    binaryfunc nb_subtract;
    binaryfunc nb_multiply;
    binaryfunc nb_remainder;
    binaryfunc nb_divmod;
    ternaryfunc nb_power;
    unaryfunc nb_negative;
    unaryfunc nb_positive;
    unaryfunc nb_absolute;
    inquiry nb_bool;
    unaryfunc nb_invert;
    binaryfunc nb_lshift;
    binaryfunc nb_rshift;
    binaryfunc nb_and;
    binaryfunc nb_xor;
    binaryfunc nb_or;
    unaryfunc nb_int;
    void *nb_reserved;  /* the slot formerly known as nb_long */
    unaryfunc nb_float;

    binaryfunc nb_inplace_add;
    binaryfunc nb_inplace_subtract;
    binaryfunc nb_inplace_multiply;
    binaryfunc nb_inplace_remainder;
    ternaryfunc nb_inplace_power;
    binaryfunc nb_inplace_lshift;
    binaryfunc nb_inplace_rshift;
    binaryfunc nb_inplace_and;
    binaryfunc nb_inplace_xor;
    binaryfunc nb_inplace_or;

    binaryfunc nb_floor_divide;
    binaryfunc nb_true_divide;
    binaryfunc nb_inplace_floor_divide;
    binaryfunc nb_inplace_true_divide;

    unaryfunc nb_index;

    binaryfunc nb_matrix_multiply;
    binaryfunc nb_inplace_matrix_multiply;
} PyNumberMethods;

typedef struct {
    lenfunc sq_length;
    binaryfunc sq_concat;
    ssizeargfunc sq_repeat;
    ssizeargfunc sq_item;
    void *was_sq_slice;
    ssizeobjargproc sq_ass_item;
    void *was_sq_ass_slice;
    objobjproc sq_contains;

    binaryfunc sq_inplace_concat;
    ssizeargfunc sq_inplace_repeat;
} PySequenceMethods;

typedef struct {
  lenfunc mp_length;
  binaryfunc mp_subscript;
  objobjargproc mp_ass_subscript;
} PyMappingMethods;

typedef struct {
  unaryfunc am_await;
  unaryfunc am_aiter;
  unaryfunc am_anext;
} PyAsyncMethods;

typedef struct {
     getbufferproc bf_getbuffer;
     releasebufferproc bf_releasebuffer;
} PyBufferProcs;

typedef void (*freefunc)(void *);
typedef void (*destructor)(PyObject *);
typedef int (*printfunc)(PyObject *, FILE *, int);
typedef PyObject *(*getattrfunc)(PyObject *, char *);
typedef PyObject *(*getattrofunc)(PyObject *, PyObject *);
typedef int (*setattrfunc)(PyObject *, char *, PyObject *);
typedef int (*setattrofunc)(PyObject *, PyObject *, PyObject *);
typedef PyObject *(*reprfunc)(PyObject *);
typedef long (*hashfunc_long)(PyObject *);
typedef Py_hash_t (*hashfunc_hash_t)(PyObject *);
typedef PyObject *(*richcmpfunc) (PyObject *, PyObject *, int);
typedef PyObject *(*getiterfunc) (PyObject *);
typedef PyObject *(*iternextfunc) (PyObject *);
typedef PyObject *(*descrgetfunc) (PyObject *, PyObject *, PyObject *);
typedef int (*descrsetfunc) (PyObject *, PyObject *, PyObject *);
typedef int (*initproc)(PyObject *, PyObject *, PyObject *);
typedef PyObject *(*newfunc)(struct _typeobject *, PyObject *, PyObject *);
typedef PyObject *(*allocfunc)(struct _typeobject *, Py_ssize_t);

typedef struct PyMemberDef {
  const char *name;
  int type;
  Py_ssize_t offset;
  int flags;
  const char *doc;
} PyMemberDef;

typedef PyObject *(*PyCFunction)(PyObject *, PyObject *);

struct PyMethodDef {
  const char  *ml_name;   /* The name of the built-in function/method */
  PyCFunction ml_meth;    /* The C function that implements it */
  int         ml_flags;   /* Combination of METH_xxx flags, which mostly
                             describe the args expected by the C func */
  const char  *ml_doc;    /* The __doc__ attribute, or NULL */
};
typedef struct PyMethodDef PyMethodDef;

/* Flag passed to newmethodobject */
#define Py_METH_VARARGS  0x0001
#define Py_METH_KEYWORDS 0x0002
#define Py_METH_NOARGS   0x0004
#define Py_METH_O        0x0008
#define Py_METH_CLASS    0x0010
#define Py_METH_STATIC   0x0020
#define Py_METH_COEXIST   0x0040
#define Py_METH_FASTCALL  0x0080

typedef struct _typeobject {
  PyObject_VAR_HEAD
  const char *tp_name; /* For printing, in format "<module>.<name>" */
  Py_ssize_t tp_basicsize, tp_itemsize; /* For allocation */

  /* Methods to implement standard operations */

  destructor tp_dealloc;
  printfunc tp_print;
  getattrfunc tp_getattr;
  setattrfunc tp_setattr;
  PyAsyncMethods *tp_as_async; /* formerly known as tp_compare (Python 2)
                                  or tp_reserved (Python 3) */
  reprfunc tp_repr;

  /* Method suites for standard classes */

  PyNumberMethods *tp_as_number;
  PySequenceMethods *tp_as_sequence;
  PyMappingMethods *tp_as_mapping;

  /* More standard operations (here for binary compatibility) */

  union {
    hashfunc_long   _long;
    hashfunc_hash_t _hash_t;
  } tp_hash;

  ternaryfunc tp_call;
  reprfunc tp_str;
  getattrofunc tp_getattro;
  setattrofunc tp_setattro;

  /* Functions to access object as input/output buffer */
  PyBufferProcs *tp_as_buffer;

  /* Flags to define presence of optional/expanded features */
  unsigned long tp_flags;

  const char *tp_doc; /* Documentation string */

  /* Assigned meaning in release 2.0 */
  /* call function for all accessible objects */
  traverseproc tp_traverse;

  /* delete references to contained objects */
  inquiry tp_clear;

  /* Assigned meaning in release 2.1 */
  /* rich comparisons */
  richcmpfunc tp_richcompare;

  /* weak reference enabler */
  Py_ssize_t tp_weaklistoffset;

  /* Iterators */
  getiterfunc tp_iter;
  iternextfunc tp_iternext;

  /* Attribute descriptor and subclassing stuff */
  struct PyMethodDef *tp_methods;
  struct PyMemberDef *tp_members;
  struct PyGetSetDef *tp_getset;
  struct _typeobject *tp_base;
  PyObject *tp_dict;
  descrgetfunc tp_descr_get;
  descrsetfunc tp_descr_set;
  Py_ssize_t tp_dictoffset;
  initproc tp_init;
  allocfunc tp_alloc;
  newfunc tp_new;
  freefunc tp_free; /* Low-level free-memory routine */
  inquiry tp_is_gc; /* For PyObject_IS_GC */
  PyObject *tp_bases;
  PyObject *tp_mro; /* method resolution order */
  PyObject *tp_cache;
  PyObject *tp_subclasses;
  PyObject *tp_weaklist;
  destructor tp_del;

  /* Type attribute cache version tag. Added in version 2.6 */
  unsigned int tp_version_tag;

  destructor tp_finalize;

#ifdef COUNT_ALLOCS
  /* these must be last and never explicitly initialized */
  Py_ssize_t tp_allocs;
  Py_ssize_t tp_frees;
  Py_ssize_t tp_maxalloc;
  struct _typeobject *tp_prev;
  struct _typeobject *tp_next;
#endif
} PyTypeObject;

/* Python 2.7 */
#define Py_TPFLAGS_HAVE_GETCHARBUFFER  (1L<<0)
#define Py_TPFLAGS_HAVE_SEQUENCE_IN    (1L<<1)
#define Py_TPFLAGS_GC                  0 /* was sometimes (0x00000001<<2) in Python <= 2.1 */
#define Py_TPFLAGS_HAVE_INPLACEOPS     (1L<<3)
#define Py_TPFLAGS_CHECKTYPES          (1L<<4)
#define Py_TPFLAGS_HAVE_RICHCOMPARE    (1L<<5)
#define Py_TPFLAGS_HAVE_WEAKREFS       (1L<<6)
#define Py_TPFLAGS_HAVE_ITER           (1L<<7)
#define Py_TPFLAGS_HAVE_CLASS          (1L<<8)
#define Py_TPFLAGS_HAVE_INDEX          (1L<<17)
#define Py_TPFLAGS_HAVE_NEWBUFFER      (1L<<21)
#define Py_TPFLAGS_STRING_SUBCLASS     (1L<<27)

/* Python 3.0+ has only the following TPFLAGS: */
#define Py_TPFLAGS_HEAPTYPE (1L<<9)
#define Py_TPFLAGS_BASETYPE (1L<<10)
#define Py_TPFLAGS_READY    (1L<<12)
#define Py_TPFLAGS_READYING (1L<<13)
#define Py_TPFLAGS_HAVE_GC  (1L<<14)
#define Py_TPFLAGS_HAVE_VERSION_TAG  (1L<<18)
#define Py_TPFLAGS_VALID_VERSION_TAG (1L<<19)
#define Py_TPFLAGS_IS_ABSTRACT    (1L<<20)
#define Py_TPFLAGS_INT_SUBCLASS   (1L<<23)
#define Py_TPFLAGS_LONG_SUBCLASS  (1L<<24)
#define Py_TPFLAGS_LIST_SUBCLASS  (1L<<25)
#define Py_TPFLAGS_TUPLE_SUBCLASS (1L<<26)
#define Py_TPFLAGS_BYTES_SUBCLASS (1L<<27)
#define Py_TPFLAGS_UNICODE_SUBCLASS  (1L<<28)
#define Py_TPFLAGS_DICT_SUBCLASS     (1L<<29)
#define Py_TPFLAGS_BASE_EXC_SUBCLASS (1L<<30)
#define Py_TPFLAGS_TYPE_SUBCLASS     (1L<<31)

/* only use this if we have the stackless extension */
#define Py_TPFLAGS_HAVE_STACKLESS_EXTENSION (3L<<15)

#define PyType_HasFeature(t, f)  (((t)->tp_flags & (f)) != 0)
#define PyType_FastSubclass(t,f)  PyType_HasFeature(t,f)

/* Member Types */
#define Py_T_SHORT     0
#define Py_T_INT       1
#define Py_T_LONG      2
#define Py_T_FLOAT     3
#define Py_T_DOUBLE    4
#define Py_T_STRING    5
#define Py_T_OBJECT    6
/* XXX the ordering here is weird for binary compatibility */
#define Py_T_CHAR      7   /* 1-character string */
#define Py_T_BYTE      8   /* 8-bit signed int */
/* unsigned variants: */
#define Py_T_UBYTE     9
#define Py_T_USHORT    10
#define Py_T_UINT      11
#define Py_T_ULONG     12

/* Added by Jack: strings contained in the structure */
#define Py_T_STRING_INPLACE    13

/* Added by Lillo: bools contained in the structure (assumed char) */
#define Py_T_BOOL      14

#define Py_T_OBJECT_EX 16  /* Like T_OBJECT, but raises AttributeError
                           when the value is NULL, instead of
                           converting to None. */
#define Py_T_LONGLONG      17
#define Py_T_ULONGLONG     18

#define Py_T_PYSSIZET      19      /* Py_ssize_t */
#define Py_T_NONE          20      /* Value is always None */


/* Member Flags */
#define Py_READONLY            1
#define Py_READ_RESTRICTED     2
#define Py_PY_WRITE_RESTRICTED 4
#define Py_RESTRICTED          (READ_RESTRICTED | PY_WRITE_RESTRICTED)

/* Rich comparison opcodes */
#define Py_LT 0
#define Py_LE 1
#define Py_EQ 2
#define Py_NE 3
#define Py_GT 4
#define Py_GE 5

#define PyType_IS_GC(t) PyType_HasFeature((t), Py_TPFLAGS_HAVE_GC)
#define PyObject_IS_GC(o) (PyType_IS_GC(Py_TYPE(o)) && \
    (Py_TYPE(o)->tp_is_gc == NULL || Py_TYPE(o)->tp_is_gc(o)))

typedef union _gc_head {
  struct {
    union _gc_head *gc_next;
    union _gc_head *gc_prev;
    Py_ssize_t gc_refs;
  } gc;
  long double dummy;  /* force worst-case alignment */
} PyGC_Head;

typedef struct {
  PyObject_HEAD
  PyObject *cl_bases; /* A tuple of class objects */
  PyObject *cl_dict;  /* A dictionary */
  PyObject *cl_name;  /* A string */
  /* The following three are functions or NULL */
  PyObject *cl_getattr;
  PyObject *cl_setattr;
  PyObject *cl_delattr;
  PyObject *cl_weakreflist; /* List of weak references */
} PyClassObject;

typedef struct {
  PyObject_HEAD
  PyClassObject *in_class;  /* The class object */
  PyObject *in_dict;        /* A dictionary */
  PyObject *in_weakreflist; /* List of weak references */
} PyInstanceObject;

/* ==== ruby_object wrapper ==== */

typedef struct {
  PyObject_HEAD
  VALUE ruby_object;
} PyRubyObject;

extern PyTypeObject PyRuby_Type;

#define PyRuby_Check(pyobj) (Py_TYPE(pyobj) == &PyRuby_Type)
#define PyRuby_get_ruby_object(pyobj) (((PyRubyObject *)(pyobj))->ruby_object)

PyObject * PyRuby_New(VALUE ruby_object);

/* ==== thread support ==== */

typedef enum {
  PyGILState_LOCKED,
  PyGILState_UNLOCKED
} PyGILState_STATE;

#if   defined(PYCALL_THREAD_WIN32)
typedef DWORD pycall_tls_key;
#elif defined(PYCALL_THREAD_PTHREAD)
typedef pthread_key_t pycall_tls_key;
#else
# error "unsupported thread type"
#endif

int pycall_tls_create(pycall_tls_key* tls_key);
void *pycall_tls_get(pycall_tls_key tls_key);
int pycall_tls_set(pycall_tls_key tls_key, void *ptr);

int pycall_without_gvl_p(void);
VALUE pycall_without_gvl(VALUE (* func)(VALUE), VALUE arg);

/* ==== pycall ==== */

typedef struct {
  PyObject *_Py_NoneStruct;
  PyObject *_Py_TrueStruct;
  PyObject *_Py_FalseStruct;

  PyTypeObject *PyBool_Type;
  PyTypeObject *PyClass_Type;
  PyTypeObject *PyComplex_Type;
  PyTypeObject *PyDict_Type;
  PyTypeObject *PyFloat_Type;
  PyTypeObject *PyInstance_Type;
  PyTypeObject *PyInt_Type;
  PyTypeObject *PyList_Type;
  PyTypeObject *PyLong_Type;
  PyTypeObject *PyModule_Type;
  PyTypeObject *PyString_Type;
  PyTypeObject *PyTuple_Type;
  PyTypeObject *PyType_Type;
  PyTypeObject *PyUnicode_Type;

  PyObject *PyExc_RuntimeError;
  PyObject *PyExc_TypeError;

  void (* Py_InitializeEx)(int);
  int (* Py_IsInitialized)();
  char const * (* Py_GetVersion)();

  void (* PySys_SetArgvEx)(int, char **, int);

  void (* Py_IncRef)(PyObject *);
  void (* Py_DecRef)(PyObject *);

  PyObject * (* _PyObject_New)(PyTypeObject *);
  int (* PyCallable_Check)(PyObject *);
  int (* PyObject_IsInstance)(PyObject *, PyObject *);
  int (* PyObject_IsSubclass)(PyObject *, PyObject *);
  union {
    long      (* _long)(PyObject *);
    Py_hash_t (* _hash_t)(PyObject *);
  } PyObject_Hash;
  PyObject * (* PyObject_RichCompare)(PyObject *, PyObject *, int);
  PyObject * (* PyObject_Call)(PyObject *, PyObject *, PyObject *);
  PyObject * (* PyObject_CallMethod)(PyObject *, char const *, char const *, ...);
  PyObject * (* PyObject_Dir)(PyObject *);
  PyObject * (* PyObject_GenericGetAttr)(PyObject *, PyObject *);
  PyObject * (* PyObject_GetAttrString)(PyObject *, char const *);
  int (* PyObject_SetAttrString)(PyObject *, char const *, PyObject *);
  int (* PyObject_HasAttrString)(PyObject *, char const *);
  int (* PyObject_DelAttrString)(PyObject *, char const *);
  PyObject * (* PyObject_GetItem)(PyObject *, PyObject *);
  int (* PyObject_SetItem)(PyObject *obj, PyObject *key, PyObject *value);
  int (* PyObject_DelItem)(PyObject *, PyObject *);
  PyObject * (* PyObject_GetIter)(PyObject *);
  PyObject * (* PyObject_Str)(PyObject *);
  PyObject * (* PyObject_Repr)(PyObject *);

  int (* PyType_Ready)(PyTypeObject *);
  PyObject * (* PyType_GenericNew)(PyTypeObject *, PyObject *, PyObject *);

  PyObject * (* PyWeakref_NewRef)(PyObject *ob, PyObject *callback);

  PyObject * (* PyBool_FromLong)(long);

  PyObject * (* PyCFunction_NewEx)(PyMethodDef *, PyObject *, PyObject *);

  double (* PyComplex_RealAsDouble)(PyObject *);
  double (* PyComplex_ImagAsDouble)(PyObject *);
  PyObject * (* PyComplex_FromDoubles)(double, double);

  double (* PyFloat_AsDouble)(PyObject *);
  PyObject * (* PyFloat_FromDouble)(double);

  PyObject * (* PyInt_FromLong)(long);
  PyObject * (* PyInt_FromSsize_t)(Py_ssize_t);
  Py_ssize_t (* PyInt_AsSsize_t)(PyObject *);

  long (* PyLong_AsLongAndOverflow)(PyObject *, int *);
  PyObject * (* PyLong_FromLong)(long);
#ifdef HAVE_LONG_LONG
  LONG_LONG (* PyLong_AsLongLongAndOverflow)(PyObject *, int *);
  PyObject * (* PyLong_FromLongLong)(LONG_LONG);
#endif
  Py_ssize_t (* PyLong_AsSsize_t)(PyObject *);

  PyObject * (* PyTuple_New)(Py_ssize_t);
  Py_ssize_t (* PyTuple_Size)(PyObject *);
  PyObject * (* PyTuple_GetItem)(PyObject *, Py_ssize_t);
  int (* PyTuple_SetItem)(PyObject *, Py_ssize_t, PyObject *);

  PyObject * (* PySlice_New)(PyObject *, PyObject *, PyObject *);

  PyObject * (* PyIter_Next)(PyObject *);

  int (* PyEval_ThreadsInitialized)(void);
  void (* PyEval_InitThreads)(void);

  PyObject * (* PyErr_Occurred)(void);
  void (* PyErr_Fetch)(PyObject **, PyObject **, PyObject **);
  void (* PyErr_Restore)(PyObject *, PyObject *, PyObject *);
  void (* PyErr_Clear)(void);
  void (* PyErr_SetString)(PyObject *, const char *);   /* decoded from utf-8 */
  void (* PyErr_Format)(PyObject *, const char *, ...); /* ASCII-encoded string  */
  void (* PyErr_SetInterrupt)(void);

  PyObject * (* PyImport_ImportModule)(char const*);
  PyObject * (* PyImport_ImportModuleLevel)(char const*, PyObject *, PyObject *, PyObject *, int);

  void (* PyOS_AfterFork)(void);

  PyObject * (* PyList_New)(Py_ssize_t);
  Py_ssize_t (* PyList_Size)(PyObject *);
  PyObject * (* PyList_GetItem)(PyObject *, Py_ssize_t);
  int (* PyList_SetItem)(PyObject *, Py_ssize_t, PyObject *);
  int (* PyList_Insert)(PyObject *, Py_ssize_t, PyObject *);
  int (* PyList_Append)(PyObject *, PyObject *);

  PyObject * (* PyDict_New)();
  int (* PyDict_Contains)(PyObject *, PyObject *);
  int (* PyDict_SetItemString)(PyObject *, char const *, PyObject *);
  int (* PyDict_Next)(PyObject *, Py_ssize_t *, PyObject **, PyObject **);

  int (* PySequence_Check)(PyObject *);
  Py_ssize_t (* PySequence_Size)(PyObject *);
  int (* PySequence_Contains)(PyObject *, PyObject *);
  PyObject * (* PySequence_GetItem)(PyObject *, Py_ssize_t);

  int (* PyString_AsStringAndSize)(PyObject *, char **, Py_ssize_t *);
  PyObject * (* PyString_FromStringAndSize)(char *, Py_ssize_t);
  PyObject * (* PyString_FromFormatV)(char const*, ...);

  PyObject * (* PyUnicode_AsUTF8String)(PyObject *);
  PyObject * (* PyUnicode_DecodeUTF8)(char const*, Py_ssize_t, char const *);
  PyObject * (* PyUnicode_FromFormatV)(char const*, ...);

  int (* PyGILState_Check)(void);
  PyGILState_STATE (* PyGILState_Ensure)(void);
  void (* PyGILState_Release)(PyGILState_STATE);
} pycall_libpython_api_table_t;

pycall_libpython_api_table_t *pycall_libpython_api_table(void);
#define Py_API(name) (pycall_libpython_api_table()->name)

int pycall_python_major_version(void);
Py_ssize_t pycall_python_hexversion(void);
#define pycall_python_long_hash (pycall_python_hexversion() < 0x03020000)

void pycall_Py_DecRef(PyObject *);

extern const rb_data_type_t pycall_pyptr_data_type;
size_t pycall_pyptr_memsize(void const *);
void pycall_pyptr_free(void *);

VALUE pycall_import_module(char const *name);
VALUE pycall_import_module_level(char const *name, VALUE globals, VALUE locals, VALUE fromlist, int level);
VALUE pycall_getattr_default(VALUE pyobj, char const *name, VALUE default_value);
VALUE pycall_getattr(VALUE pyobj, char const *name);

VALUE pycall_pytype_to_ruby(PyObject *);
VALUE pycall_pymodule_to_ruby(PyObject *);
VALUE pycall_pybool_to_ruby(PyObject *);
VALUE pycall_pycomplex_to_ruby(PyObject *);
VALUE pycall_pyfloat_to_ruby(PyObject *);
VALUE pycall_pyint_to_ruby(PyObject *);
VALUE pycall_pylong_to_ruby(PyObject *);
VALUE pycall_pystring_to_ruby(PyObject *);
VALUE pycall_pyunicode_to_ruby(PyObject *);
VALUE pycall_pyobject_to_a(PyObject *);

VALUE pycall_conv_to_str(VALUE);

PyObject *pycall_pyobject_from_ruby(VALUE);
PyObject *pycall_pystring_from_ruby(VALUE);
PyObject *pycall_pytuple_from_ruby(VALUE);
PyObject *pycall_pylist_from_ruby(VALUE);
PyObject *pycall_pydict_from_ruby(VALUE);
PyObject *pycall_pyslice_from_ruby(VALUE);

RBIMPL_ATTR_NONNULL((1))
RBIMPL_ATTR_FORMAT(RBIMPL_PRINTF_FORMAT, 1, 0)
NORETURN(void pycall_pyerror_fetch_and_raise(char const *format, ...));

unsigned long pycall_default_tp_flags(void);
PyObject *pycall_pystring_from_format(char const *format, ...);
PyObject *pycall_pystring_from_formatv(char const *format, va_list vargs);

VALUE pycall_pyrubyptr_new(PyObject *pyrubyobj);

int pycall_obj_is_step_range(VALUE obj);
int pycall_extract_range(VALUE obj, VALUE *pbegin, VALUE *pend, int *pexclude_end, VALUE *pstep);

void pycall_gcguard_register(PyObject *, VALUE);
void pycall_gcguard_delete(PyObject *);
void pycall_gcguard_register_pyrubyobj(PyObject *);
void pycall_gcguard_unregister_pyrubyobj(PyObject *);

void pycall_init_libpython_api_table(VALUE handle);
void pycall_init_exceptions(VALUE handle);
void pycall_init_gcguard(void);
void pycall_init_ruby_wrapper(void);

#define pycall_hash_salt_32 0xb592cd9b
extern intptr_t pycall_hash_salt;
extern VALUE pycall_mPyCall;
extern VALUE pycall_cPyPtr;
extern VALUE pycall_eError;

#define mPyCall pycall_mPyCall
#define cPyPtr pycall_cPyPtr
#define eError pycall_eError

/* #define PYCALL_DEBUG_DUMP_REFCNT */

#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
} /* extern "C" { */
#endif

#endif /* PYCALL_INTERNAL_H */
