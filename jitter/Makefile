# Copyright (c) 2017 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

all:
	gcc -O1 jitter.c -fstack-protector-all -fPIE -Wformat -Wformat-security -Wl,-z,noexecstack,-z,relro,-z,now -o jitter

pt:
	gcc -O1 jitter.c -fstack-protector-all -fPIE -Wformat -Wformat-security -Wl,-z,noexecstack,-z,relro,-z,now -o jitter -D PROCESSOR_TRACE

clean:
	rm -f jitter
