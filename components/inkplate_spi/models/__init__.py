class InkplateModel:
    models = {}

    def __init__(self, name, cpp_class, width, height, init_sequence=None):
        self.name = name
        self.cpp_class = cpp_class
        self.width = width
        self.height = height
        self.init_sequence = init_sequence or []
        InkplateModel.models[name] = self

    def get_init_bytes(self):
        """Flatten to wire format: [chip, cmd, n_data, data_bytes ...] per entry."""
        result = []
        for entry in self.init_sequence:
            chip = entry[0]
            cmd  = entry[1]
            data = list(entry[2:])
            result += [chip, cmd, len(data)] + data
        return result
