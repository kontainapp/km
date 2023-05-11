/*
 * Copyright 2023 Kontain Inc
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

#ifndef __KM_IOCONTEXT_H__
#define __KM_IOCONTEXT_H__

int km_iocontext_recover(unsigned int nr_events, aio_context_t pcontext);
int km_iocontext_add(unsigned int nr_events, aio_context_t* pcontextp);
int km_iocontext_remove(aio_context_t pcontext);
int km_iocontext_xlate_p2k(aio_context_t pcontext, aio_context_t* kcontext);
size_t km_fs_iocontext_notes_length(void);
size_t km_fs_iocontext_notes_write(char* buf, size_t length);
int km_fs_recover_iocontexts(char* ptr, size_t length);
void km_iocontext_init(void);
void km_iocontext_deinit(void);

#endif   // !defined(__KM_IOCONTEXT_H__)
