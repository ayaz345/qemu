/*
 * Human Monitor Interface commands
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "qemu/help_option.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-run-state.h"
#include "qapi/qapi-commands-stats.h"
#include "qapi/qapi-commands-tpm.h"
#include "qapi/qapi-commands-virtio.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qemu/cutils.h"
#include "hw/core/cpu.h"
#include "hw/intc/intc.h"

bool hmp_handle_error(Monitor *mon, Error *err)
{
    if (err) {
        error_reportf_err(err, "Error: ");
        return true;
    }
    return false;
}

/*
 * Split @str at comma.
 * A null @str defaults to "".
 */
strList *hmp_split_at_comma(const char *str)
{
    char **split = g_strsplit(str ?: "", ",", -1);
    strList *res = NULL;
    strList **tail = &res;
    int i;

    for (i = 0; split[i]; i++) {
        QAPI_LIST_APPEND(tail, split[i]);
    }

    g_free(split);
    return res;
}

void hmp_info_name(Monitor *mon, const QDict *qdict)
{
    NameInfo *info;

    info = qmp_query_name(NULL);
    if (info->name) {
        monitor_printf(mon, "%s\n", info->name);
    }
    qapi_free_NameInfo(info);
}

void hmp_info_version(Monitor *mon, const QDict *qdict)
{
    VersionInfo *info;

    info = qmp_query_version(NULL);

    monitor_printf(mon, "%" PRId64 ".%" PRId64 ".%" PRId64 "%s\n",
                   info->qemu->major, info->qemu->minor, info->qemu->micro,
                   info->package);

    qapi_free_VersionInfo(info);
}

void hmp_info_status(Monitor *mon, const QDict *qdict)
{
    StatusInfo *info;

    info = qmp_query_status(NULL);

    monitor_printf(mon, "VM status: %s%s",
                   info->running ? "running" : "paused",
                   info->singlestep ? " (single step mode)" : "");

    if (!info->running && info->status != RUN_STATE_PAUSED) {
        monitor_printf(mon, " (%s)", RunState_str(info->status));
    }

    monitor_printf(mon, "\n");

    qapi_free_StatusInfo(info);
}

static int hmp_info_pic_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    Monitor *mon = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        if (k->print_info) {
            k->print_info(intc, mon);
        } else {
            monitor_printf(mon, "Interrupt controller information not available for %s.\n",
                           object_get_typename(obj));
        }
    }

    return 0;
}

void hmp_info_pic(Monitor *mon, const QDict *qdict)
{
    object_child_foreach_recursive(object_get_root(),
                                   hmp_info_pic_foreach, mon);
}

void hmp_info_tpm(Monitor *mon, const QDict *qdict)
{
#ifdef CONFIG_TPM
    TPMInfoList *info_list, *info;
    Error *err = NULL;
    unsigned int c = 0;
    TPMPassthroughOptions *tpo;
    TPMEmulatorOptions *teo;

    info_list = qmp_query_tpm(&err);
    if (err) {
        monitor_printf(mon, "TPM device not supported\n");
        error_free(err);
        return;
    }

    if (info_list) {
        monitor_printf(mon, "TPM device:\n");
    }

    for (info = info_list; info; info = info->next) {
        TPMInfo *ti = info->value;
        monitor_printf(mon, " tpm%d: model=%s\n",
                       c, TpmModel_str(ti->model));

        monitor_printf(mon, "  \\ %s: type=%s",
                       ti->id, TpmType_str(ti->options->type));

        switch (ti->options->type) {
        case TPM_TYPE_PASSTHROUGH:
            tpo = ti->options->u.passthrough.data;
            monitor_printf(mon, "%s%s%s%s",
                           tpo->path ? ",path=" : "",
                           tpo->path ?: "",
                           tpo->cancel_path ? ",cancel-path=" : "",
                           tpo->cancel_path ?: "");
            break;
        case TPM_TYPE_EMULATOR:
            teo = ti->options->u.emulator.data;
            monitor_printf(mon, ",chardev=%s", teo->chardev);
            break;
        case TPM_TYPE__MAX:
            break;
        }
        monitor_printf(mon, "\n");
        c++;
    }
    qapi_free_TPMInfoList(info_list);
#else
    monitor_printf(mon, "TPM device not supported\n");
#endif /* CONFIG_TPM */
}

void hmp_quit(Monitor *mon, const QDict *qdict)
{
    monitor_suspend(mon);
    qmp_quit(NULL);
}

void hmp_stop(Monitor *mon, const QDict *qdict)
{
    qmp_stop(NULL);
}

void hmp_sync_profile(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");

    if (op == NULL) {
        bool on = qsp_is_enabled();

        monitor_printf(mon, "sync-profile is %s\n", on ? "on" : "off");
        return;
    }
    if (!strcmp(op, "on")) {
        qsp_enable();
    } else if (!strcmp(op, "off")) {
        qsp_disable();
    } else if (!strcmp(op, "reset")) {
        qsp_reset();
    } else {
        Error *err = NULL;

        error_setg(&err, QERR_INVALID_PARAMETER, op);
        hmp_handle_error(mon, err);
    }
}

void hmp_exit_preconfig(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_x_exit_preconfig(&err);
    hmp_handle_error(mon, err);
}

void hmp_cpu(Monitor *mon, const QDict *qdict)
{
    int64_t cpu_index;

    /* XXX: drop the monitor_set_cpu() usage when all HMP commands that
            use it are converted to the QAPI */
    cpu_index = qdict_get_int(qdict, "index");
    if (monitor_set_cpu(mon, cpu_index) < 0) {
        monitor_printf(mon, "invalid CPU index\n");
    }
}

void hmp_cont(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_cont(&err);
    hmp_handle_error(mon, err);
}

void hmp_change(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_str(qdict, "device");
    const char *target = qdict_get_str(qdict, "target");
    const char *arg = qdict_get_try_str(qdict, "arg");
    const char *read_only = qdict_get_try_str(qdict, "read-only-mode");
    bool force = qdict_get_try_bool(qdict, "force", false);
    Error *err = NULL;

#ifdef CONFIG_VNC
    if (strcmp(device, "vnc") == 0) {
        hmp_change_vnc(mon, device, target, arg, read_only, force, &err);
    } else
#endif
    {
        hmp_change_medium(mon, device, target, arg, read_only, force, &err);
    }

    hmp_handle_error(mon, err);
}

void hmp_getfd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_getfd(fdname, &err);
    hmp_handle_error(mon, err);
}

void hmp_closefd(Monitor *mon, const QDict *qdict)
{
    const char *fdname = qdict_get_str(qdict, "fdname");
    Error *err = NULL;

    qmp_closefd(fdname, &err);
    hmp_handle_error(mon, err);
}

void hmp_info_iothreads(Monitor *mon, const QDict *qdict)
{
    IOThreadInfoList *info_list = qmp_query_iothreads(NULL);
    IOThreadInfoList *info;
    IOThreadInfo *value;

    for (info = info_list; info; info = info->next) {
        value = info->value;
        monitor_printf(mon, "%s:\n", value->id);
        monitor_printf(mon, "  thread_id=%" PRId64 "\n", value->thread_id);
        monitor_printf(mon, "  poll-max-ns=%" PRId64 "\n", value->poll_max_ns);
        monitor_printf(mon, "  poll-grow=%" PRId64 "\n", value->poll_grow);
        monitor_printf(mon, "  poll-shrink=%" PRId64 "\n", value->poll_shrink);
        monitor_printf(mon, "  aio-max-batch=%" PRId64 "\n",
                       value->aio_max_batch);
    }

    qapi_free_IOThreadInfoList(info_list);
}

static void print_stats_schema_value(Monitor *mon, StatsSchemaValue *value)
{
    const char *unit = NULL;
    monitor_printf(mon, "    %s (%s%s", value->name, StatsType_str(value->type),
                   value->has_unit || value->exponent ? ", " : "");

    if (value->has_unit) {
        if (value->unit == STATS_UNIT_SECONDS) {
            unit = "s";
        } else if (value->unit == STATS_UNIT_BYTES) {
            unit = "B";
        }
    }

    if (unit && value->base == 10 &&
        value->exponent >= -18 && value->exponent <= 18 &&
        value->exponent % 3 == 0) {
        monitor_puts(mon, si_prefix(value->exponent));
    } else if (unit && value->base == 2 &&
               value->exponent >= 0 && value->exponent <= 60 &&
               value->exponent % 10 == 0) {

        monitor_puts(mon, iec_binary_prefix(value->exponent));
    } else if (value->exponent) {
        /* Use exponential notation and write the unit's English name */
        monitor_printf(mon, "* %d^%d%s",
                       value->base, value->exponent,
                       value->has_unit ? " " : "");
        unit = NULL;
    }

    if (value->has_unit) {
        monitor_puts(mon, unit ? unit : StatsUnit_str(value->unit));
    }

    /* Print bucket size for linear histograms */
    if (value->type == STATS_TYPE_LINEAR_HISTOGRAM && value->has_bucket_size) {
        monitor_printf(mon, ", bucket size=%d", value->bucket_size);
    }
    monitor_printf(mon, ")");
}

static StatsSchemaValueList *find_schema_value_list(
    StatsSchemaList *list, StatsProvider provider,
    StatsTarget target)
{
    StatsSchemaList *node;

    for (node = list; node; node = node->next) {
        if (node->value->provider == provider &&
            node->value->target == target) {
            return node->value->stats;
        }
    }
    return NULL;
}

static void print_stats_results(Monitor *mon, StatsTarget target,
                                bool show_provider,
                                StatsResult *result,
                                StatsSchemaList *schema)
{
    /* Find provider schema */
    StatsSchemaValueList *schema_value_list =
        find_schema_value_list(schema, result->provider, target);
    StatsList *stats_list;

    if (!schema_value_list) {
        monitor_printf(mon, "failed to find schema list for %s\n",
                       StatsProvider_str(result->provider));
        return;
    }

    if (show_provider) {
        monitor_printf(mon, "provider: %s\n",
                       StatsProvider_str(result->provider));
    }

    for (stats_list = result->stats; stats_list;
             stats_list = stats_list->next,
             schema_value_list = schema_value_list->next) {

        Stats *stats = stats_list->value;
        StatsValue *stats_value = stats->value;
        StatsSchemaValue *schema_value = schema_value_list->value;

        /* Find schema entry */
        while (!g_str_equal(stats->name, schema_value->name)) {
            if (!schema_value_list->next) {
                monitor_printf(mon, "failed to find schema entry for %s\n",
                               stats->name);
                return;
            }
            schema_value_list = schema_value_list->next;
            schema_value = schema_value_list->value;
        }

        print_stats_schema_value(mon, schema_value);

        if (stats_value->type == QTYPE_QNUM) {
            monitor_printf(mon, ": %" PRId64 "\n", stats_value->u.scalar);
        } else if (stats_value->type == QTYPE_QBOOL) {
            monitor_printf(mon, ": %s\n", stats_value->u.boolean ? "yes" : "no");
        } else if (stats_value->type == QTYPE_QLIST) {
            uint64List *list;
            int i;

            monitor_printf(mon, ": ");
            for (list = stats_value->u.list, i = 1;
                 list;
                 list = list->next, i++) {
                monitor_printf(mon, "[%d]=%" PRId64 " ", i, list->value);
            }
            monitor_printf(mon, "\n");
        }
    }
}

/* Create the StatsFilter that is needed for an "info stats" invocation.  */
static StatsFilter *stats_filter(StatsTarget target, const char *names,
                                 int cpu_index, StatsProvider provider)
{
    StatsFilter *filter = g_malloc0(sizeof(*filter));
    StatsProvider provider_idx;
    StatsRequestList *request_list = NULL;

    filter->target = target;
    switch (target) {
    case STATS_TARGET_VM:
        break;
    case STATS_TARGET_VCPU:
    {
        strList *vcpu_list = NULL;
        CPUState *cpu = qemu_get_cpu(cpu_index);
        char *canonical_path = object_get_canonical_path(OBJECT(cpu));

        QAPI_LIST_PREPEND(vcpu_list, canonical_path);
        filter->u.vcpu.has_vcpus = true;
        filter->u.vcpu.vcpus = vcpu_list;
        break;
    }
    default:
        break;
    }

    if (!names && provider == STATS_PROVIDER__MAX) {
        return filter;
    }

    /*
     * "info stats" can only query either one or all the providers.  Querying
     * by name, but not by provider, requires the creation of one filter per
     * provider.
     */
    for (provider_idx = 0; provider_idx < STATS_PROVIDER__MAX; provider_idx++) {
        if (provider == STATS_PROVIDER__MAX || provider == provider_idx) {
            StatsRequest *request = g_new0(StatsRequest, 1);
            request->provider = provider_idx;
            if (names && !g_str_equal(names, "*")) {
                request->has_names = true;
                request->names = hmp_split_at_comma(names);
            }
            QAPI_LIST_PREPEND(request_list, request);
        }
    }

    filter->has_providers = true;
    filter->providers = request_list;
    return filter;
}

void hmp_info_stats(Monitor *mon, const QDict *qdict)
{
    const char *target_str = qdict_get_str(qdict, "target");
    const char *provider_str = qdict_get_try_str(qdict, "provider");
    const char *names = qdict_get_try_str(qdict, "names");

    StatsProvider provider = STATS_PROVIDER__MAX;
    StatsTarget target;
    Error *err = NULL;
    g_autoptr(StatsSchemaList) schema = NULL;
    g_autoptr(StatsResultList) stats = NULL;
    g_autoptr(StatsFilter) filter = NULL;
    StatsResultList *entry;

    target = qapi_enum_parse(&StatsTarget_lookup, target_str, -1, &err);
    if (err) {
        monitor_printf(mon, "invalid stats target %s\n", target_str);
        goto exit_no_print;
    }
    if (provider_str) {
        provider = qapi_enum_parse(&StatsProvider_lookup, provider_str, -1, &err);
        if (err) {
            monitor_printf(mon, "invalid stats provider %s\n", provider_str);
            goto exit_no_print;
        }
    }

    schema = qmp_query_stats_schemas(provider_str ? true : false,
                                     provider, &err);
    if (err) {
        goto exit;
    }

    switch (target) {
    case STATS_TARGET_VM:
        filter = stats_filter(target, names, -1, provider);
        break;
    case STATS_TARGET_VCPU: {}
        int cpu_index = monitor_get_cpu_index(mon);
        filter = stats_filter(target, names, cpu_index, provider);
        break;
    default:
        abort();
    }

    stats = qmp_query_stats(filter, &err);
    if (err) {
        goto exit;
    }
    for (entry = stats; entry; entry = entry->next) {
        print_stats_results(mon, target, provider_str == NULL, entry->value, schema);
    }

exit:
    if (err) {
        monitor_printf(mon, "%s\n", error_get_pretty(err));
    }
exit_no_print:
    error_free(err);
}

static void hmp_virtio_dump_protocols(Monitor *mon,
                                      VhostDeviceProtocols *pcol)
{
    strList *pcol_list = pcol->protocols;
    while (pcol_list) {
        monitor_printf(mon, "\t%s", pcol_list->value);
        pcol_list = pcol_list->next;
        if (pcol_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    if (pcol->has_unknown_protocols) {
        monitor_printf(mon, "  unknown-protocols(0x%016"PRIx64")\n",
                       pcol->unknown_protocols);
    }
}

static void hmp_virtio_dump_status(Monitor *mon,
                                   VirtioDeviceStatus *status)
{
    strList *status_list = status->statuses;
    while (status_list) {
        monitor_printf(mon, "\t%s", status_list->value);
        status_list = status_list->next;
        if (status_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    if (status->has_unknown_statuses) {
        monitor_printf(mon, "  unknown-statuses(0x%016"PRIx32")\n",
                       status->unknown_statuses);
    }
}

static void hmp_virtio_dump_features(Monitor *mon,
                                     VirtioDeviceFeatures *features)
{
    strList *transport_list = features->transports;
    while (transport_list) {
        monitor_printf(mon, "\t%s", transport_list->value);
        transport_list = transport_list->next;
        if (transport_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }

    monitor_printf(mon, "\n");
    strList *list = features->dev_features;
    if (list) {
        while (list) {
            monitor_printf(mon, "\t%s", list->value);
            list = list->next;
            if (list != NULL) {
                monitor_printf(mon, ",\n");
            }
        }
        monitor_printf(mon, "\n");
    }

    if (features->has_unknown_dev_features) {
        monitor_printf(mon, "  unknown-features(0x%016"PRIx64")\n",
                       features->unknown_dev_features);
    }
}

void hmp_virtio_query(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    VirtioInfoList *list = qmp_x_query_virtio(&err);
    VirtioInfoList *node;

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    if (list == NULL) {
        monitor_printf(mon, "No VirtIO devices\n");
        return;
    }

    node = list;
    while (node) {
        monitor_printf(mon, "%s [%s]\n", node->value->path,
                       node->value->name);
        node = node->next;
    }
    qapi_free_VirtioInfoList(list);
}

void hmp_virtio_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    VirtioStatus *s = qmp_x_query_virtio_status(path, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:             %s %s\n",
                   s->name, s->vhost_dev ? "(vhost)" : "");
    monitor_printf(mon, "  device_id:               %d\n", s->device_id);
    monitor_printf(mon, "  vhost_started:           %s\n",
                   s->vhost_started ? "true" : "false");
    monitor_printf(mon, "  bus_name:                %s\n", s->bus_name);
    monitor_printf(mon, "  broken:                  %s\n",
                   s->broken ? "true" : "false");
    monitor_printf(mon, "  disabled:                %s\n",
                   s->disabled ? "true" : "false");
    monitor_printf(mon, "  disable_legacy_check:    %s\n",
                   s->disable_legacy_check ? "true" : "false");
    monitor_printf(mon, "  started:                 %s\n",
                   s->started ? "true" : "false");
    monitor_printf(mon, "  use_started:             %s\n",
                   s->use_started ? "true" : "false");
    monitor_printf(mon, "  start_on_kick:           %s\n",
                   s->start_on_kick ? "true" : "false");
    monitor_printf(mon, "  use_guest_notifier_mask: %s\n",
                   s->use_guest_notifier_mask ? "true" : "false");
    monitor_printf(mon, "  vm_running:              %s\n",
                   s->vm_running ? "true" : "false");
    monitor_printf(mon, "  num_vqs:                 %"PRId64"\n", s->num_vqs);
    monitor_printf(mon, "  queue_sel:               %d\n",
                   s->queue_sel);
    monitor_printf(mon, "  isr:                     %d\n", s->isr);
    monitor_printf(mon, "  endianness:              %s\n",
                   s->device_endian);
    monitor_printf(mon, "  status:\n");
    hmp_virtio_dump_status(mon, s->status);
    monitor_printf(mon, "  Guest features:\n");
    hmp_virtio_dump_features(mon, s->guest_features);
    monitor_printf(mon, "  Host features:\n");
    hmp_virtio_dump_features(mon, s->host_features);
    monitor_printf(mon, "  Backend features:\n");
    hmp_virtio_dump_features(mon, s->backend_features);

    if (s->vhost_dev) {
        monitor_printf(mon, "  VHost:\n");
        monitor_printf(mon, "    nvqs:           %d\n",
                       s->vhost_dev->nvqs);
        monitor_printf(mon, "    vq_index:       %"PRId64"\n",
                       s->vhost_dev->vq_index);
        monitor_printf(mon, "    max_queues:     %"PRId64"\n",
                       s->vhost_dev->max_queues);
        monitor_printf(mon, "    n_mem_sections: %"PRId64"\n",
                       s->vhost_dev->n_mem_sections);
        monitor_printf(mon, "    n_tmp_sections: %"PRId64"\n",
                       s->vhost_dev->n_tmp_sections);
        monitor_printf(mon, "    backend_cap:    %"PRId64"\n",
                       s->vhost_dev->backend_cap);
        monitor_printf(mon, "    log_enabled:    %s\n",
                       s->vhost_dev->log_enabled ? "true" : "false");
        monitor_printf(mon, "    log_size:       %"PRId64"\n",
                       s->vhost_dev->log_size);
        monitor_printf(mon, "    Features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->features);
        monitor_printf(mon, "    Acked features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->acked_features);
        monitor_printf(mon, "    Backend features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->backend_features);
        monitor_printf(mon, "    Protocol features:\n");
        hmp_virtio_dump_protocols(mon, s->vhost_dev->protocol_features);
    }

    qapi_free_VirtioStatus(s);
}

void hmp_vhost_queue_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtVhostQueueStatus *s =
        qmp_x_query_virtio_vhost_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:          %s (vhost)\n",
                   s->name);
    monitor_printf(mon, "  kick:                 %"PRId64"\n", s->kick);
    monitor_printf(mon, "  call:                 %"PRId64"\n", s->call);
    monitor_printf(mon, "  VRing:\n");
    monitor_printf(mon, "    num:         %"PRId64"\n", s->num);
    monitor_printf(mon, "    desc:        0x%016"PRIx64"\n", s->desc);
    monitor_printf(mon, "    desc_phys:   0x%016"PRIx64"\n",
                   s->desc_phys);
    monitor_printf(mon, "    desc_size:   %"PRId32"\n", s->desc_size);
    monitor_printf(mon, "    avail:       0x%016"PRIx64"\n", s->avail);
    monitor_printf(mon, "    avail_phys:  0x%016"PRIx64"\n",
                   s->avail_phys);
    monitor_printf(mon, "    avail_size:  %"PRId32"\n", s->avail_size);
    monitor_printf(mon, "    used:        0x%016"PRIx64"\n", s->used);
    monitor_printf(mon, "    used_phys:   0x%016"PRIx64"\n",
                   s->used_phys);
    monitor_printf(mon, "    used_size:   %"PRId32"\n", s->used_size);

    qapi_free_VirtVhostQueueStatus(s);
}

void hmp_virtio_queue_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtQueueStatus *s = qmp_x_query_virtio_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:          %s\n", s->name);
    monitor_printf(mon, "  queue_index:          %d\n", s->queue_index);
    monitor_printf(mon, "  inuse:                %d\n", s->inuse);
    monitor_printf(mon, "  used_idx:             %d\n", s->used_idx);
    monitor_printf(mon, "  signalled_used:       %d\n",
                   s->signalled_used);
    monitor_printf(mon, "  signalled_used_valid: %s\n",
                   s->signalled_used_valid ? "true" : "false");
    if (s->has_last_avail_idx) {
        monitor_printf(mon, "  last_avail_idx:       %d\n",
                       s->last_avail_idx);
    }
    if (s->has_shadow_avail_idx) {
        monitor_printf(mon, "  shadow_avail_idx:     %d\n",
                       s->shadow_avail_idx);
    }
    monitor_printf(mon, "  VRing:\n");
    monitor_printf(mon, "    num:          %"PRId32"\n", s->vring_num);
    monitor_printf(mon, "    num_default:  %"PRId32"\n",
                   s->vring_num_default);
    monitor_printf(mon, "    align:        %"PRId32"\n",
                   s->vring_align);
    monitor_printf(mon, "    desc:         0x%016"PRIx64"\n",
                   s->vring_desc);
    monitor_printf(mon, "    avail:        0x%016"PRIx64"\n",
                   s->vring_avail);
    monitor_printf(mon, "    used:         0x%016"PRIx64"\n",
                   s->vring_used);

    qapi_free_VirtQueueStatus(s);
}

void hmp_virtio_queue_element(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    int index = qdict_get_try_int(qdict, "index", -1);
    VirtioQueueElement *e;
    VirtioRingDescList *list;

    e = qmp_x_query_virtio_queue_element(path, queue, index != -1,
                                         index, &err);
    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name: %s\n", e->name);
    monitor_printf(mon, "  index:   %d\n", e->index);
    monitor_printf(mon, "  desc:\n");
    monitor_printf(mon, "    descs:\n");

    list = e->descs;
    while (list) {
        monitor_printf(mon, "        addr 0x%"PRIx64" len %d",
                       list->value->addr, list->value->len);
        if (list->value->flags) {
            strList *flag = list->value->flags;
            monitor_printf(mon, " (");
            while (flag) {
                monitor_printf(mon, "%s", flag->value);
                flag = flag->next;
                if (flag) {
                    monitor_printf(mon, ", ");
                }
            }
            monitor_printf(mon, ")");
        }
        list = list->next;
        if (list) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    monitor_printf(mon, "  avail:\n");
    monitor_printf(mon, "    flags: %d\n", e->avail->flags);
    monitor_printf(mon, "    idx:   %d\n", e->avail->idx);
    monitor_printf(mon, "    ring:  %d\n", e->avail->ring);
    monitor_printf(mon, "  used:\n");
    monitor_printf(mon, "    flags: %d\n", e->used->flags);
    monitor_printf(mon, "    idx:   %d\n", e->used->idx);

    qapi_free_VirtioQueueElement(e);
}
