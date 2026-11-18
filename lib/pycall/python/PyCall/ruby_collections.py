"""Read-only Python views of Ruby's Array, Hash, and Set.

The classes in this module implement the ``collections.abc.Sequence``,
``Mapping``, and ``Set`` protocols on top of a Ruby object without copying
it.  Elements are converted to Python objects with PyCall's usual rules when
they are accessed, not when the view is created.

Instances are created on the Ruby side by ``PyCall.wrap_ruby_collection``,
which selects the class by the type of the Ruby object.

They are not subclasses of ``list``, ``dict``, or ``set``.
"""

import operator
from collections.abc import Mapping, Sequence, Set

__all__ = ["RubyArray", "RubyHash", "RubySet"]

# Bound methods of PyCall::RubyCollectionAccessor, set by _setup().
# The accessor methods never raise, because a Ruby exception raised in a
# Ruby method called from Python is not propagated to Python.
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


class _RubyCollection(object):
    __slots__ = ("_ruby_object",)

    def __init__(self, ruby_object):
        self._ruby_object = ruby_object

    @property
    def ruby_object(self):
        """The wrapped Ruby object."""
        return self._ruby_object

    def __len__(self):
        return _length(self._ruby_object)


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
            found, value = _fetch_index(self._ruby_object, index)
            if found:
                return value
        raise IndexError("RubyArray index out of range")

    def __iter__(self):
        index = 0
        while True:
            found, value = _fetch_index(self._ruby_object, index)
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
        found, value = _fetch_key(self._ruby_object, key)
        if found:
            return value
        raise KeyError(key)

    def __iter__(self):
        return iter(_keys(self._ruby_object))

    def __contains__(self, key):
        return _has_key(self._ruby_object, key)

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
        return iter(_to_a(self._ruby_object))

    def __contains__(self, item):
        return _include(self._ruby_object, item)

    __hash__ = None

    def __repr__(self):
        return "RubySet(%r)" % (list(self),)
