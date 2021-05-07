
from kontain import snapshots

data_in = snapshots.getdata()
print("data_in:", data_in)
snapshots.putdata("Hello from python")
