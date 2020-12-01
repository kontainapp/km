
import kontain

print(kontain)
print("before")
kontain.snapshot_take(label="my label", description="snapshot for test", live=False)
print("after")
