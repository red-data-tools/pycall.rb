import collections.abc


def call(f, *args):
    """Calls f(*args) and returns its result, or the name of the exception it raised."""
    try:
        return f(*args)
    except Exception as e:
        return type(e).__name__


def getitem(obj, key):
    return call(lambda: obj[key])


def getslice(obj, start, stop):
    return call(lambda: obj[start:stop])


def contains(obj, item):
    return item in obj


def to_list(obj):
    return list(obj)


def to_dict(obj):
    return dict(obj)


def equals(a, b):
    return a == b


def not_equals(a, b):
    return a != b


def is_sequence(obj):
    return isinstance(obj, collections.abc.Sequence) and not isinstance(obj, list)


def is_mapping(obj):
    return isinstance(obj, collections.abc.Mapping) and not isinstance(obj, dict)


def is_set(obj):
    return isinstance(obj, collections.abc.Set) and not isinstance(obj, (set, frozenset))


def is_unhashable(obj):
    return call(hash, obj) == "TypeError"


def index(obj, item):
    return obj.index(item)


def get(obj, key, default):
    return obj.get(key, default)


def keyword_args(obj):
    return (lambda **kwargs: kwargs)(**obj)


def set_and(obj, other):
    return call(lambda: obj & set(other))


def set_type_of_and(obj, other):
    return type(obj & set(other)).__name__


def type_name(obj):
    return type(obj).__name__


_OPERATIONS = {
    "len": lambda obj: len(obj),
    "getitem": lambda obj, key: obj[key],
    "contains": lambda obj, item: item in obj,
    "get": lambda obj, key, default: obj.get(key, default),
    "list": lambda obj: list(obj),
    "dict": lambda obj: dict(obj),
    "eq": lambda obj, other: obj == other,
    "le": lambda obj, other: obj <= other,
}


def attempt(operation, obj, *args):
    """Runs an operation and returns its result, or "ExceptionName: message"."""
    try:
        return _OPERATIONS[operation](obj, *args)
    except Exception as e:
        return "%s: %s" % (type(e).__name__, e)


def first_two(obj, between):
    """Returns the first two items of iter(obj), calling between() after the first."""
    it = iter(obj)
    first = next(it)
    between()
    return [first, next(it)]
