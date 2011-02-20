/*
    Copyright 2011 Daniel Poelzleithner <ulatencyd at poelzi dot org>

    This file is part of ulatencyd.

    ulatencyd is free software: you can redistribute it and/or modify it under 
    the terms of the GNU General Public License as published by the 
    Free Software Foundation, either version 3 of the License, 
    or (at your option) any later version.

    ulatencyd is distributed in the hope that it will be useful, 
    but WITHOUT ANY WARRANTY; without even the implied warranty of 
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License 
    along with ulatencyd. If not, see http://www.gnu.org/licenses/.
*/
#define _GNU_SOURCE


#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "simplerules"
#endif

#include "config.h"
#include "ulatency.h"
#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <sys/stat.h>
#include <fnmatch.h>

int simplerules_id;

static GList *target_rules;

struct simple_rule {
  gid_t     gid;
  uid_t     uid;
  char     *cmdline;
  char     *exe;
  char     *basename;
  GRegex   *re_exe;
  GRegex   *re_cmd;
  GRegex   *re_basename;
  u_flag   *template;
};


int parse_line(char *line, int lineno) {
    char **chunks;
    GError *error = NULL;
    gint chunk_len;
    struct simple_rule *rule = NULL;
    int i;
    char *value, *key;
    int tmp;


    if(line[0] == '#')
        return TRUE;
    if(strlen(line) == 0)
        return TRUE;

    g_shell_parse_argv(line, 
                       &chunk_len,
                       &chunks,
                       &error);
    if(error) {
        g_warning("can't parse line %d: %s", lineno, error->message);
        goto error;
    }

    if(chunk_len && chunk_len < 2) {
        g_warning("not enough arguments in line %d: %s", lineno, line);
        goto error;
    }

    rule = g_slice_new0(struct simple_rule);

    if(chunks[0][0] == '/') {
        rule->exe = g_strdup(chunks[0]);

    } else if(!strncmp(chunks[0], "re_exe:", 7)) {
        rule->re_exe = g_regex_new(chunks[0] + 7, G_REGEX_OPTIMIZE, 0, &error);
        if(error && error->code) {
            g_warning("Error compiling regular expression in %s: %s", chunks[0], error->message);
            goto error;
        }
    } else if(!strncmp(chunks[0], "re_cmd:", 7)) {
        rule->re_cmd = g_regex_new(chunks[0] + 7, G_REGEX_OPTIMIZE, 0, &error);
        if(error && error->code) {
            g_warning("Error compiling regular expression in %s: %s", chunks[0], error->message);
            goto error;
        }
    } else if(!strncmp(chunks[0], "re_base:", 8)) {
        rule->re_cmd = g_regex_new(chunks[0] + 7, G_REGEX_OPTIMIZE, 0, &error);
        if(error && error->code) {
            g_warning("Error compiling regular expression in %s: %s", chunks[0], error->message);
            goto error;
        }
    } else {
        rule->basename = g_strdup(chunks[0]);
    }
    rule->template = g_slice_new0(u_flag);
    rule->template->name = g_strdup(chunks[1]);

    for(i = 2; chunks[i]; i++) {
        key = chunks[i];
        value = strstr(chunks[i], "=");

        if(!value) {
            g_error("invalid argument in line %d: '=' missing", lineno);
            goto error;
        }
        // split by replacing = with null byte
        *value = 0;
        value++;

        if(strcmp(key, "reason") == 0) {
            rule->template->reason = g_strdup(value);
        } else if(strcmp(key, "timeout") == 0) {
            rule->template->timeout = (time_t)atoll(value);
        } else if(strcmp(key, "priority") == 0) {
            rule->template->priority = (int32_t)atoi(value);
        } else if(strcmp(key, "value") == 0) {
            rule->template->value = (int64_t)atoll(value);
        } else if(strcmp(key, "threshold") == 0) {
            rule->template->threshold = (int64_t)atoll(value);
        } else if(strcmp(key, "value") == 0) {
            rule->template->value = (int64_t)atoll(value);
        } else if(strcmp(key, "value") == 0) {
            tmp = atoi(value);
            rule->template->inherit = tmp;
        }
    }

    target_rules = g_list_append(target_rules, rule);

    g_strfreev(chunks);
    return TRUE;
error:
    g_slice_free(struct simple_rule, rule);
    g_error_free(error);
    return FALSE;

}


int load_simple_file(const char *path) {
    char *content, **lines, *line;
    gsize length;
    int i;
    GError *error = NULL;

    if(!g_file_get_contents(path,
                            &content,
                            &length,
                            &error)) {
        g_warning("can't load simple rule file %s: %s", path, error->message);
        return FALSE;
    }

    g_debug("load simple rule file: %s", path);

    printf("%s\n", content);

    lines = g_strsplit_set(content, "\n", -1);
    for(i = 0; lines[i]; i++) {
        line = lines[i];

        parse_line(line, i+1);

    }
    g_strfreev(lines);
    g_free(content);

    return TRUE;
}


int load_simple_directory(char *path) {
    char rpath[PATH_MAX+1];
    gsize  disabled_len;
    int i, j;
    char **disabled;
    char *rule_name = NULL;
    struct stat sb;

    disabled = g_key_file_get_string_list(config_data, "simplerules",
                                          "disabled_rules", &disabled_len, NULL);


    g_message("load simple rules directory: %s", path);


    struct dirent **namelist;
    int n;

    n = scandir(path, &namelist, 0, versionsort);
    if (n < 0) {
       g_warning("cant't load directory %s", path);
       return FALSE;
    } else {
       for(i = 0; i < n; i++) {

          if(fnmatch("*.conf", namelist[i]->d_name, 0))
            continue;
          rule_name = g_strndup(namelist[i]->d_name,strlen(namelist[i]->d_name)-4);

          for(j = 0; j < disabled_len; j++) {
            if(!g_strcasecmp(disabled[j], rule_name))
              goto skip;
          }

          snprintf(rpath, PATH_MAX, "%s/%s", path, namelist[i]->d_name);
          if (stat(rpath, &sb) == -1)
              goto skip;
          if((sb.st_mode & S_IFMT) != S_IFREG)
              goto next;

          load_simple_file(rpath);

      next:
          g_free(rule_name);
          rule_name = NULL;

          free(namelist[i]);
          continue;
      skip:
          g_debug("skip rule: %s", namelist[i]->d_name);
          g_free(rule_name);
          rule_name = NULL;

          free(namelist[i]);
       }
       free(namelist);
    }
    return TRUE;
}

void read_rules(void) {
    load_simple_directory(QUOTEME(CONFIG_PATH)"/simple.d");
    load_simple_file(QUOTEME(CONFIG_PATH)"/simple.conf");

    return;
}

int rule_applies(u_proc *proc, struct simple_rule *rule) {
    if(rule->cmdline) {
        if(u_proc_ensure(proc, CMDLINE, FALSE) && 
           !strncmp(proc->cmdline_match, rule->cmdline, strlen(rule->cmdline))) {
//              printf("cmdline %s  %s\n", proc->cmdline_match, rule->cmdline);
              return TRUE;
           }
    }
    if(rule->basename) {
        if(u_proc_ensure(proc, CMDLINE, FALSE) && 
           !strncmp(proc->cmdfile, rule->basename, strlen(rule->basename))) {
//              printf("cmdfile %s  %s\n", proc->cmdfile, rule->basename);
              return TRUE;
        }
    }
    if(rule->exe) {
        if(u_proc_ensure(proc, EXE, FALSE) && 
           !strncmp(proc->exe, rule->exe, strlen(rule->exe))) {
//              printf("exe %s  %s\n", proc->exe, rule->exe);
              return TRUE;
           }
    }
    if(rule->re_exe) {
        if(u_proc_ensure(proc, EXE, FALSE) && 
           g_regex_match(rule->re_exe, proc->exe, 0, NULL)) {
//              printf("re_exe %s  %p\n", proc->exe, rule->re_exe);
              return TRUE;
        }
    }
    if(rule->re_cmd) {
        if(u_proc_ensure(proc, CMDLINE, FALSE) && 
           g_regex_match(rule->re_cmd, proc->cmdline_match, 0, NULL)) {
//              printf("re_cmd %s  %p\n", proc->cmdline_match, rule->re_cmd);
              return TRUE;
           }
    }
    if(rule->re_basename) {
        if(u_proc_ensure(proc, CMDLINE, FALSE) && 
           g_regex_match(rule->re_basename, proc->cmdfile, 0, NULL)) {
//              printf("re_base %s  %p\n", proc->exe, rule->re_basename);
              return TRUE;
           }
    }
    return FALSE;
}

void simple_add_flag(u_filter *filter, u_proc *proc, struct simple_rule *rule) {
    u_flag *t = rule->template;
    u_flag *nf = u_flag_new(filter, t->name);

    if(t->reason)
        nf->reason  = g_strdup(t->reason);
    if(t->timeout)
        nf->timeout = time(NULL) + t->timeout;
    nf->priority    = t->priority;
    nf->value       = t->value;
    nf->threshold   = t->threshold;
    nf->inherit     = t->inherit;

//    printf("add flag %s %d\n", nf->name, proc->pid); 

    u_flag_add(proc, nf);
}

int simplerules_run_proc(u_proc *proc, u_filter *filter) {
    GList *cur = target_rules;
    struct simple_rule *rule;

    while(cur) {
        rule = cur->data;

        if(rule_applies(proc, rule)) {
            simple_add_flag(filter, proc, rule);
        }
        cur = g_list_next(cur);
    }
    return FILTER_MIX(FILTER_STOP, 0);
}


int simplerules_init() {
    simplerules_id = get_plugin_id();
    u_filter *filter;
    target_rules = NULL;
    read_rules();
    if(target_rules) {
        filter = filter_new();
        filter->type = FILTER_C;
        filter->name = g_strdup("simplerules");
        filter->callback = simplerules_run_proc;
        filter_register(filter);
    }
    return 0;
}

