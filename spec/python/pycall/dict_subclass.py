import collections


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


class BadItemsDict(dict):
    def items(self):
        return ['not a pair']


def moved_ordered_dict():
    """An OrderedDict whose order differs from its underlying dict storage."""
    d = collections.OrderedDict([('a', 1), ('b', 2), ('c', 3)])
    d.move_to_end('a')
    return d
