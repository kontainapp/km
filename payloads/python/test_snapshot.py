
from kontain import snapshots

print("before")
snapshots.take(label="my label", description="snapshot for test", live=False)
print("after")
