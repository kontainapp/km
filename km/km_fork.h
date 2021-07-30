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

#ifndef __KM_FORK_H__
#define __KM_FORK_H__

extern void km_forward_sigchild(int signo, siginfo_t* sinfo, void* ucontext_unused);
extern int km_before_fork(km_vcpu_t* vcpu, km_hc_args_t* arg, uint8_t is_clone);
extern int km_dofork(int* in_child);

#endif /* !defined(__KM_FORK_H__) */
