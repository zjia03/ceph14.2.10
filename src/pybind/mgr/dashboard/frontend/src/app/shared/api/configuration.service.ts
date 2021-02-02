import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';

import { ConfigFormCreateRequestModel } from '../../ceph/cluster/configuration/configuration-form/configuration-form-create-request.model';
import { ApiModule } from './api.module';

@Injectable({
  providedIn: ApiModule
})
export class ConfigurationService {
  constructor(private http: HttpClient) {}

  getConfigData() {
    return this.http.get('api/cluster_conf/');
  }

  get(configOption: string) {
    return this.http.get(`api/cluster_conf/${configOption}`);
  }

  filter(configOptionNames: Array<string>) {
    return this.http.get(`api/cluster_conf/filter?names=${configOptionNames.join(',')}`);
  }

  create(configOption: ConfigFormCreateRequestModel) {
    return this.http.post('api/cluster_conf/', configOption);
  }

  delete(configOption: string, section: string) {
    return this.http.delete(`api/cluster_conf/${configOption}?section=${section}`);
  }

  bulkCreate(configOptions: Object) {
    return this.http.put('api/cluster_conf/', configOptions);
  }
}
