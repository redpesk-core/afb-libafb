/*
 * Copyright (C) 2020-2021 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, something express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

extern char *expand_vars_array(const char *value, int copy, char ***varsarray);
extern char *expand_vars_only(const char *value, int copy, char **vars);
extern char *expand_vars_env_only(const char *value, int copy);
extern char *expand_vars(const char *value, int copy, char **before, char **after);
extern char *expand_vars_first(const char *value, int copy, char **vars);
extern char *expand_vars_last(const char *value, int copy, char **vars);
