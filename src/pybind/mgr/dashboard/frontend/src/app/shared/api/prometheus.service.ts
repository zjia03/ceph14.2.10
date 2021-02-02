import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';

import { Observable } from 'rxjs';
import { map } from 'rxjs/operators';

import { AlertmanagerSilence } from '../models/alertmanager-silence';
import {
  AlertmanagerAlert,
  AlertmanagerNotification,
  PrometheusRuleGroup
} from '../models/prometheus-alerts';
import { ApiModule } from './api.module';
import { SettingsService } from './settings.service';

@Injectable({
  providedIn: ApiModule
})
export class PrometheusService {
  private baseURL = 'api/prometheus';
  private settingsKey = {
    alertmanager: 'api/settings/alertmanager-api-host',
    prometheus: 'api/settings/prometheus-api-host'
  };

  constructor(private http: HttpClient, private settingsService: SettingsService) {}

  ifAlertmanagerConfigured(fn, elseFn?): void {
    this.settingsService.ifSettingConfigured(this.settingsKey.alertmanager, fn, elseFn);
  }

  disableAlertmanagerConfig(): void {
    this.settingsService.disableSetting(this.settingsKey.alertmanager);
  }

  ifPrometheusConfigured(fn, elseFn?): void {
    this.settingsService.ifSettingConfigured(this.settingsKey.prometheus, fn, elseFn);
  }

  disablePrometheusConfig(): void {
    this.settingsService.disableSetting(this.settingsKey.prometheus);
  }

  getAlerts(params = {}): Observable<AlertmanagerAlert[]> {
    return this.http.get<AlertmanagerAlert[]>(this.baseURL, { params });
  }

  getSilences(params = {}): Observable<AlertmanagerSilence[]> {
    return this.http.get<AlertmanagerSilence[]>(`${this.baseURL}/silences`, { params });
  }

  getRules(
    type: 'all' | 'alerting' | 'rewrites' = 'all'
  ): Observable<{ groups: PrometheusRuleGroup[] }> {
    let rules = this.http.get<{ groups: PrometheusRuleGroup[] }>(`${this.baseURL}/rules`);
    const filterByType = (_type: 'alerting' | 'rewrites') => {
      return rules.pipe(
        map((_rules) => {
          _rules.groups = _rules.groups.map((group) => {
            group.rules = group.rules.filter((rule) => rule.type === _type);
            return group;
          });
          return _rules;
        })
      );
    };
    switch (type) {
      case 'alerting':
      case 'rewrites':
        rules = filterByType(type);
        break;
    }
    return rules;
  }

  setSilence(silence: AlertmanagerSilence) {
    return this.http.post(`${this.baseURL}/silence`, silence, { observe: 'response' });
  }

  expireSilence(silenceId: string) {
    return this.http.delete(`${this.baseURL}/silence/${silenceId}`, { observe: 'response' });
  }

  getNotifications(
    notification?: AlertmanagerNotification
  ): Observable<AlertmanagerNotification[]> {
    const url = `${this.baseURL}/notifications?from=${
      notification && notification.id ? notification.id : 'last'
    }`;
    return this.http.get<AlertmanagerNotification[]>(url);
  }
}
