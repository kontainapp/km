/*
 * Copyright 2021 Kontain Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package app.kontain.snapshots;

/**
 * Java API for Kontain snapshots. A Kontain snapshot is a restartable guest process
 * image.
 * 
 * @version 1.0
 */
public class Snapshot {
    // Loads dynamic library when this is class is loaded
    static {
        System.loadLibrary("km_java_native");
    }

    // native helper
    native int take_native(String label, String description);

    /**
     * Creates a KM snapshot for the current process. The current 
     * process stops when the snapshot is created.
     * 
     * @param label       Snapshot label used in filename.
     * @param description Descriptive text to go with snapshot.
     * @return 0 if successful, -1 if a failure.
     */
    public int take(String label, String description) {
        return take_native(label, description);
    }

    public static void main(String[] args) {
        System.out.println("Kontain API");
        new Snapshot().take("test_snap", "Testing snapshot");
		System.out.println("past snapshot");
    }
}
