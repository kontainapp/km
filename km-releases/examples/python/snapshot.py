#
# Copyright © 2019-2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#

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
