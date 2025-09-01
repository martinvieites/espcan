import json

class Esp32Can:
    def __init__(self, identifier, status):
        self.identifier = identifier
        self.status = status
        self.monitoring = 0
        self.vin = ""
        self.pids = []
        self.dtc_count = 0
        self.dtc_list = []

    def __str__(self):
        return f"id:{self.identifier}, status:{self.status}, data:{self.data}, pids:{self.pids}"

    def __repr__(self):
        return "[ " + str(self) + " ]"

    def set_status(self, status):
        self.status = int(status)

    def get_status(self):
        if self.status:
            return "connected"
        else:
            return "disconnected"

    def set_pids(self, pids):
        self.pids = pids

    def add_pids(self, pids):
        self.pids += pids

    def have_pids(self):
        return len(self.pids) > 0

    def set_vin(self, vin):
        self.vin = vin.decode("utf-8")

    def get_vin(self):
        return self.vin

    def set_dtc(self, count):
        self.dtc_count = count

    def reset_dtc(self):
        self.dtc_count = 0
        self.dtc_list = []

    def add_dtc(self, dtc):
        self.dtc_list.append(dtc)

    def toJson(self):
        return json.dumps(self, default=lambda o: o.__dict__)
