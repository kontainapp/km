# Copyright 2020 Kontain Inc. All rights reserved.

def load_model():
    '''
    Simulate loading a large ML model
    '''
    import time
    time.sleep(3)
    return {'a': 'Hello world'}


# load model globally
model = load_model()


def func():
    '''
    demo function
    '''
    print(model['a'])


# Take a snapshot of the process if this is running with KM
try:
    from kontain import snapshots
    snapshots.take(live=True)
except:
    pass

if __name__ == "__main__":
    func()
