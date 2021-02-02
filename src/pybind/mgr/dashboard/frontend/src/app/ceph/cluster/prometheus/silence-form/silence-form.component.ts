import { Component } from '@angular/core';
import { Validators } from '@angular/forms';
import { ActivatedRoute, Router } from '@angular/router';
import { I18n } from '@ngx-translate/i18n-polyfill';

import * as _ from 'lodash';
import { BsModalService } from 'ngx-bootstrap/modal';

import { PrometheusService } from '../../../../shared/api/prometheus.service';
import {
  ActionLabelsI18n,
  SucceededActionLabelsI18n
} from '../../../../shared/constants/app.constants';
import { Icons } from '../../../../shared/enum/icons.enum';
import { NotificationType } from '../../../../shared/enum/notification-type.enum';
import { CdFormBuilder } from '../../../../shared/forms/cd-form-builder';
import { CdFormGroup } from '../../../../shared/forms/cd-form-group';
import { CdValidators } from '../../../../shared/forms/cd-validators';
import {
  AlertmanagerSilence,
  AlertmanagerSilenceMatcher,
  AlertmanagerSilenceMatcherMatch
} from '../../../../shared/models/alertmanager-silence';
import { Permission } from '../../../../shared/models/permissions';
import { AlertmanagerAlert, PrometheusRule } from '../../../../shared/models/prometheus-alerts';
import { AuthStorageService } from '../../../../shared/services/auth-storage.service';
import { NotificationService } from '../../../../shared/services/notification.service';
import { PrometheusSilenceMatcherService } from '../../../../shared/services/prometheus-silence-matcher.service';
import { TimeDiffService } from '../../../../shared/services/time-diff.service';
import { SilenceMatcherModalComponent } from '../silence-matcher-modal/silence-matcher-modal.component';

@Component({
  selector: 'cd-prometheus-form',
  templateUrl: './silence-form.component.html',
  styleUrls: ['./silence-form.component.scss']
})
export class SilenceFormComponent {
  icons = Icons;
  permission: Permission;
  form: CdFormGroup;
  rules: PrometheusRule[];

  // Date formatting rules can be found here: https://momentjs.com/docs/#/displaying/format/
  bsConfig = { dateInputFormat: 'YYYY-MM-DDT HH:mm' };

  recreate = false;
  edit = false;
  id: string;

  action: string;
  resource = this.i18n('silence');

  matchers: AlertmanagerSilenceMatcher[] = [];
  matcherMatch: AlertmanagerSilenceMatcherMatch = undefined;
  matcherConfig = [
    {
      tooltip: this.i18n('Attribute name'),
      icon: this.icons.paragraph,
      attribute: 'name'
    },
    {
      tooltip: this.i18n('Value'),
      icon: this.icons.terminal,
      attribute: 'value'
    },
    {
      tooltip: this.i18n('Regular expression'),
      icon: this.icons.magic,
      attribute: 'isRegex'
    }
  ];

  constructor(
    private i18n: I18n,
    private router: Router,
    private authStorageService: AuthStorageService,
    private formBuilder: CdFormBuilder,
    private prometheusService: PrometheusService,
    private notificationService: NotificationService,
    private route: ActivatedRoute,
    private timeDiff: TimeDiffService,
    private bsModalService: BsModalService,
    private silenceMatcher: PrometheusSilenceMatcherService,
    private actionLabels: ActionLabelsI18n,
    private succeededLabels: SucceededActionLabelsI18n
  ) {
    this.init();
  }

  private init() {
    this.chooseMode();
    this.authenticate();
    this.createForm();
    this.setupDates();
    this.getData();
  }

  private chooseMode() {
    this.edit = this.router.url.startsWith('/monitoring/silence/edit');
    this.recreate = this.router.url.startsWith('/monitoring/silence/recreate');
    if (this.edit) {
      this.action = this.actionLabels.EDIT;
    } else if (this.recreate) {
      this.action = this.actionLabels.RECREATE;
    } else {
      this.action = this.actionLabels.CREATE;
    }
  }

  private authenticate() {
    this.permission = this.authStorageService.getPermissions().prometheus;
    const allowed =
      this.permission.read && (this.edit ? this.permission.update : this.permission.create);
    if (!allowed) {
      this.router.navigate(['/404']);
    }
  }

  private createForm() {
    this.form = this.formBuilder.group(
      {
        startsAt: [null, [Validators.required]],
        duration: ['2h', [Validators.min(1)]],
        endsAt: [null, [Validators.required]],
        createdBy: [this.authStorageService.getUsername(), [Validators.required]],
        comment: [null, [Validators.required]]
      },
      {
        validators: CdValidators.custom('matcherRequired', () => this.matchers.length === 0)
      }
    );
  }

  private setupDates() {
    const now = new Date();
    now.setSeconds(0, 0); // Normalizes start date
    this.form.silentSet('startsAt', now);
    this.updateDate();
    this.subscribeDateChanges();
  }

  private updateDate(updateStartDate?: boolean) {
    const next = this.timeDiff.calculateDate(
      this.form.getValue(updateStartDate ? 'endsAt' : 'startsAt'),
      this.form.getValue('duration'),
      updateStartDate
    );
    if (next) {
      this.form.silentSet(updateStartDate ? 'startsAt' : 'endsAt', next);
    }
  }

  private subscribeDateChanges() {
    this.form.get('startsAt').valueChanges.subscribe(() => {
      this.onDateChange();
    });
    this.form.get('duration').valueChanges.subscribe(() => {
      this.updateDate();
    });
    this.form.get('endsAt').valueChanges.subscribe(() => {
      this.onDateChange(true);
    });
  }

  private onDateChange(updateStartDate?: boolean) {
    if (this.form.getValue('startsAt') < this.form.getValue('endsAt')) {
      this.updateDuration();
    } else {
      this.updateDate(updateStartDate);
    }
  }

  private updateDuration() {
    this.form.silentSet(
      'duration',
      this.timeDiff.calculateDuration(this.form.getValue('startsAt'), this.form.getValue('endsAt'))
    );
  }

  private getData() {
    this.getRules();
    this.getModeSpecificData();
  }

  private getRules() {
    this.prometheusService.ifPrometheusConfigured(
      () =>
        this.prometheusService.getRules().subscribe(
          (groups) => {
            this.rules = groups['groups'].reduce(
              (acc, group) => _.concat<PrometheusRule>(acc, group.rules),
              []
            );
          },
          () => {
            this.prometheusService.disablePrometheusConfig();
            this.rules = [];
          }
        ),
      () => {
        this.rules = [];
        this.notificationService.show(
          NotificationType.info,
          this.i18n(
            'Please add your Prometheus host to the dashboard configuration and refresh the page'
          ),
          undefined,
          undefined,
          'Prometheus'
        );
      }
    );
  }

  private getModeSpecificData() {
    this.route.params.subscribe((params: { id: string }) => {
      if (!params.id) {
        return;
      }
      if (this.edit || this.recreate) {
        this.prometheusService.getSilences(params).subscribe((silences) => {
          this.fillFormWithSilence(silences[0]);
        });
      } else {
        this.prometheusService.getAlerts(params).subscribe((alerts) => {
          this.fillFormByAlert(alerts[0]);
        });
      }
    });
  }

  private fillFormWithSilence(silence: AlertmanagerSilence) {
    this.id = silence.id;
    if (this.edit) {
      ['startsAt', 'endsAt'].forEach((attr) => this.form.silentSet(attr, new Date(silence[attr])));
      this.updateDuration();
    }
    ['createdBy', 'comment'].forEach((attr) => this.form.silentSet(attr, silence[attr]));
    this.matchers = silence.matchers;
    this.validateMatchers();
  }

  private validateMatchers() {
    if (!this.rules) {
      window.setTimeout(() => this.validateMatchers(), 100);
      return;
    }
    this.matcherMatch = this.silenceMatcher.multiMatch(this.matchers, this.rules);
    this.form.markAsDirty();
    this.form.updateValueAndValidity();
  }

  private fillFormByAlert(alert: AlertmanagerAlert) {
    const labels = alert.labels;
    Object.keys(labels).forEach((key) =>
      this.setMatcher({
        name: key,
        value: labels[key],
        isRegex: false
      })
    );
  }

  private setMatcher(matcher: AlertmanagerSilenceMatcher, index?: number) {
    if (_.isNumber(index)) {
      this.matchers[index] = matcher;
    } else {
      this.matchers.push(matcher);
    }
    this.validateMatchers();
  }

  showMatcherModal(index?: number) {
    const modalRef = this.bsModalService.show(SilenceMatcherModalComponent);
    const modal = modalRef.content as SilenceMatcherModalComponent;
    modal.rules = this.rules;
    if (_.isNumber(index)) {
      modal.editMode = true;
      modal.preFillControls(this.matchers[index]);
    }
    modalRef.content.submitAction.subscribe((matcher: AlertmanagerSilenceMatcher) => {
      this.setMatcher(matcher, index);
    });
  }

  deleteMatcher(index: number) {
    this.matchers.splice(index, 1);
    this.validateMatchers();
  }

  submit() {
    if (this.form.invalid) {
      return;
    }
    this.prometheusService.setSilence(this.getSubmitData()).subscribe(
      (resp) => {
        this.router.navigate(['/monitoring'], { fragment: 'silences' });
        this.notificationService.show(
          NotificationType.success,
          this.getNotificationTile(resp.body['silenceId']),
          undefined,
          undefined,
          'Prometheus'
        );
      },
      () => this.form.setErrors({ cdSubmitButton: true })
    );
  }

  private getSubmitData(): AlertmanagerSilence {
    const payload = this.form.value;
    delete payload.duration;
    payload.startsAt = payload.startsAt.toISOString();
    payload.endsAt = payload.endsAt.toISOString();
    payload.matchers = this.matchers;
    if (this.edit) {
      payload.id = this.id;
    }
    return payload;
  }

  private getNotificationTile(id: string) {
    let action;
    if (this.edit) {
      action = this.succeededLabels.EDITED;
    } else if (this.recreate) {
      action = this.succeededLabels.RECREATED;
    } else {
      action = this.succeededLabels.CREATED;
    }
    return `${action} ${this.resource} ${id}`;
  }
}
