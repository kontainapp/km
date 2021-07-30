#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
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
