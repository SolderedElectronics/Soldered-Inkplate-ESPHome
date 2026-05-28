class InkplateModel:
    models = {}

    def __init__(self, name, cpp_class, width, height,
                 pins=None, init_sequence=None, min_update_interval_ms=0,
                 spi_data_rate=10_000_000):
        self.name = name
        self.cpp_class = cpp_class
        self.width = width
        self.height = height
        self.pins = pins or {}
        self.init_sequence = init_sequence or []
        self.min_update_interval_ms = min_update_interval_ms
        self.spi_data_rate = spi_data_rate
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
