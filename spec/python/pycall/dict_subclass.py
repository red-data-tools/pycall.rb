import collections
import sys


class PlainDictSubclass(dict):
    """A dict subclass that changes nothing, like pandas' PrettyDict."""
    pass


class VirtualItemsDict(dict):
    """A dict subclass whose Python-level view differs from its storage."""

    def items(self):
        return [('virtual', 42)]

    def __iter__(self):
        return iter(['virtual'])

    def __contains__(self, key):
        return key == 'virtual'


class PairsDict(dict):
    """A dict subclass whose items() returns the given list as it is."""

    def __init__(self, pairs):
        super().__init__()
        self.pairs = pairs

    def items(self):
        return self.pairs


class ReversedTuple(tuple):
    """A tuple subclass that Python unpacks through its __iter__."""

    def __iter__(self):
        return iter([self[1], self[0]])


def pairs_dict(kind):
    """A PairsDict whose items() yields pairs of the given kind."""
    return PairsDict({
        'list pairs': [['a', 1], ['b', 2]],
        'iterator pairs': [iter(['a', 1]), iter(['b', 2])],
        'tuple subclass pairs': [ReversedTuple((1, 'a')), ReversedTuple((2, 'b'))],
        'too few in tuple': [('a',)],
        'too many in tuple': [('a', 1, 2)],
        'too few in list': [['a']],
        'too many in list': [['a', 1, 2]],
        'empty iterator': [iter([])],
        'too few in iterator': [iter(['a'])],
        'too many in iterator': [iter(['a', 1, 2])],
        'non-iterable': [1],
    }[kind])


def moved_ordered_dict():
    """An OrderedDict whose order differs from its underlying dict storage."""
    d = collections.OrderedDict([('a', 1), ('b', 2), ('c', 3)])
    d.move_to_end('a')
    return d


class Unconvertible:
    """Instances fail to convert once a raising type mapping is registered."""
    pass


class Marker:
    pass


def retained_pair_dict(container, position):
    """A PairsDict holding one pair with an Unconvertible at `position`.

    The pair stays referenced by the dict, so its reference count can be
    compared before and after iteration.
    """
    elements = [Marker(), Marker()]
    elements[0 if position == 'key' else 1] = Unconvertible()
    pair = tuple(elements) if container == 'tuple' else list(elements)
    return PairsDict([pair])


def pair_refcounts(d):
    """Reference counts of the retained pair and of its value element."""
    pair = d.pairs[0]
    return [sys.getrefcount(pair), sys.getrefcount(pair[1])]
