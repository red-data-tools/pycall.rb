"""Read-only Python views of Ruby's Array, Hash, and Set.

The classes in this module implement the ``collections.abc.Sequence``,
``Mapping``, and ``Set`` protocols on top of a Ruby object without copying
it into Python objects.  Elements are converted to Python objects with
PyCall's usual rules when they are accessed, not when the view is created.

Iterating over a ``RubyHash`` or a ``RubySet`` first takes a snapshot of the
Ruby keys or elements as a Ruby Array, which is not converted; each of them
is converted when the iteration reaches it.

Instances are created on the Ruby side by ``PyCall.wrap_as_python_collection``,
which selects the class by the type of the Ruby object.

They are not subclasses of ``list``, ``dict``, or ``set``.

Distinct Ruby keys or elements can become equal in Python, for example the
String ``"a"`` and the Symbol ``:a``, or the Integer ``1`` and the Float
``1.0``.  Iterating over a view that has such keys or elements raises
``ValueError``, and so does looking up a ``str`` while both the String and
the Symbol are present.  ``len()`` returns the number of Ruby keys or
elements.
"""

import operator
from collections.abc import Mapping, Sequence, Set

__all__ = ["RubyArray", "RubyHash", "RubySet"]

# Status codes returned by PyCall::RubyCollectionAccessor.
_OK = 0
_MISSING = 1
_ERROR = 2
_COLLISION = 3

# Bound methods of PyCall::RubyCollectionAccessor, set by _setup().
_length = None
_fetch_index = None
_fetch_key = None
_has_key = None
_keys = None
_include = None
_to_a = None


def _setup(accessor):
    global _length, _fetch_index, _fetch_key, _has_key, _keys, _include, _to_a
    _length = getattr(accessor, "collection_length")
    _fetch_index = getattr(accessor, "array_fetch")
    _fetch_key = getattr(accessor, "hash_fetch")
    _has_key = getattr(accessor, "hash_has_key")
    _keys = getattr(accessor, "hash_keys")
    _include = getattr(accessor, "set_include")
    _to_a = getattr(accessor, "set_to_a")


def _call(method, *args):
    """Calls an accessor method and returns ``(found, value)``.

    Raises RuntimeError when the Ruby method failed, and ValueError when a
    str matched both a Ruby String and a Ruby Symbol.
    """
    result = method(*args)
    if result is None:
        # A Ruby exception that is not a StandardError is not caught by the
        # accessor, and the bridge returns None for it.
        raise RuntimeError("a Ruby method called by PyCall.ruby_collections failed")
    status, value = result
    if status == _ERROR:
        raise RuntimeError("Ruby raised %s" % (value,))
    if status == _COLLISION:
        raise ValueError(value)
    return status == _OK, value


def _iter_snapshot(snapshot, kind):
    """Iterates over a Ruby Array snapshot, converting one element at a time.

    Raises ValueError when two elements are equal in Python.
    """
    seen = set()
    index = 0
    while True:
        found, value = _call(_fetch_index, snapshot, index)
        if not found:
            return
        try:
            duplicated = value in seen
            seen.add(value)
        except TypeError:
            # An unhashable value cannot be equal to a key of a dict or an
            # element of a set, so it is not checked.
            duplicated = False
        if duplicated:
            raise ValueError(
                "two Ruby %ss are equal to %r in Python" % (kind, value))
        yield value
        index += 1


class _RubyCollection(object):
    __slots__ = ("_ruby_object",)

    def __init__(self, ruby_object):
        self._ruby_object = ruby_object

    @property
    def ruby_object(self):
        """The wrapped Ruby object."""
        return self._ruby_object

    def __len__(self):
        return _call(_length, self._ruby_object)[1]


class RubyArray(_RubyCollection, Sequence):
    """A read-only ``Sequence`` view of a Ruby Array."""

    __slots__ = ()

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self)))]
        index = operator.index(index)
        if index < 0:
            index += len(self)
        if index >= 0:
            found, value = _call(_fetch_index, self._ruby_object, index)
            if found:
                return value
        raise IndexError("RubyArray index out of range")

    def __iter__(self):
        index = 0
        while True:
            found, value = _call(_fetch_index, self._ruby_object, index)
            if not found:
                return
            yield value
            index += 1

    def __eq__(self, other):
        if isinstance(other, (list, RubyArray)):
            if len(self) != len(other):
                return False
            return all(a == b for a, b in zip(self, other))
        return NotImplemented

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    __hash__ = None

    def __repr__(self):
        return "RubyArray(%r)" % (list(self),)


class RubyHash(_RubyCollection, Mapping):
    """A read-only ``Mapping`` view of a Ruby Hash."""

    __slots__ = ()

    def __getitem__(self, key):
        found, value = _call(_fetch_key, self._ruby_object, key)
        if found:
            return value
        raise KeyError(key)

    def __iter__(self):
        return _iter_snapshot(_call(_keys, self._ruby_object)[1], "key")

    def __contains__(self, key):
        return _call(_has_key, self._ruby_object, key)[0]

    __hash__ = None

    def __repr__(self):
        return "RubyHash(%r)" % (dict(self.items()),)


class RubySet(_RubyCollection, Set):
    """A read-only ``Set`` view of a Ruby Set."""

    __slots__ = ()

    @classmethod
    def _from_iterable(cls, iterable):
        # Results of set operations such as `&` and `|` are Python sets.
        return set(iterable)

    def __iter__(self):
        return _iter_snapshot(_call(_to_a, self._ruby_object)[1], "element")

    def __contains__(self, item):
        return _call(_include, self._ruby_object, item)[0]

    # Unlike those of collections.abc.Set, the comparisons always iterate, so
    # that a collision raises ValueError even when len() alone could decide
    # the result.
    def _compare(self, other, op):
        if not isinstance(other, Set):
            return NotImplemented
        if not isinstance(other, (set, frozenset)):
            other = set(other)
        return op(set(self), other)

    def __eq__(self, other):
        return self._compare(other, operator.eq)

    def __ne__(self, other):
        return self._compare(other, operator.ne)

    def __le__(self, other):
        return self._compare(other, operator.le)

    def __lt__(self, other):
        return self._compare(other, operator.lt)

    def __ge__(self, other):
        return self._compare(other, operator.ge)

    def __gt__(self, other):
        return self._compare(other, operator.gt)

    __hash__ = None

    def __repr__(self):
        return "RubySet(%r)" % (list(self),)
