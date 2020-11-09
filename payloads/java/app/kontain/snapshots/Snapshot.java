/*
 * Copyright 2020 Kontain Inc. All rights reserved.
 *
 * Kontain Inc CONFIDENTIAL
 *
 * This file includes unpublished proprietary source code of Kontain Inc. The
 * copyright notice above does not evidence any actual or intended publication
 * of such source code. Disclosure of this source code or any related
 * proprietary information is strictly prohibited without the express written
 * permission of Kontain Inc.
 */
package app.kontain.snapshots;

/**
 * Java API for Kontain services.
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
