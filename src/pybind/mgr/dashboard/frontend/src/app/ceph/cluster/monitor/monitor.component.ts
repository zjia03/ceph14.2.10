import { Component } from '@angular/core';

import { I18n } from '@ngx-translate/i18n-polyfill';
import * as _ from 'lodash';

import { MonitorService } from '../../../shared/api/monitor.service';
import { CellTemplate } from '../../../shared/enum/cell-template.enum';

@Component({
  selector: 'cd-monitor',
  templateUrl: './monitor.component.html',
  styleUrls: ['./monitor.component.scss']
})
export class MonitorComponent {
  mon_status: any;
  inQuorum: any;
  notInQuorum: any;

  interval: any;

  constructor(private monitorService: MonitorService, private i18n: I18n) {
    this.inQuorum = {
      columns: [
        { prop: 'name', name: this.i18n('Name'), cellTransformation: CellTemplate.routerLink },
        { prop: 'rank', name: this.i18n('Rank') },
        { prop: 'public_addr', name: this.i18n('Public Address') },
        {
          prop: 'cdOpenSessions',
          name: this.i18n('Open Sessions'),
          cellTransformation: CellTemplate.sparkline,
          comparator: (dataA, dataB) => {
            // We get the last value of time series to compare:
            const lastValueA = _.last(dataA);
            const lastValueB = _.last(dataB);

            if (!lastValueA || !lastValueB || lastValueA === lastValueB) {
              return 0;
            }

            return lastValueA > lastValueB ? 1 : -1;
          }
        }
      ],
      data: []
    };

    this.notInQuorum = {
      columns: [
        { prop: 'name', name: this.i18n('Name'), cellTransformation: CellTemplate.routerLink },
        { prop: 'rank', name: this.i18n('Rank') },
        { prop: 'public_addr', name: this.i18n('Public Address') }
      ],
      data: []
    };
  }

  refresh() {
    this.monitorService.getMonitor().subscribe((data: any) => {
      data.in_quorum.map((row) => {
        row.cdOpenSessions = row.stats.num_sessions.map((i) => i[1]);
        row.cdLink = '/perf_counters/mon/' + row.name;
        row.cdParams = { fromLink: '/monitor' };
        return row;
      });

      data.out_quorum.map((row) => {
        row.cdLink = '/perf_counters/mon/' + row.name;
        row.cdParams = { fromLink: '/monitor' };
        return row;
      });

      this.inQuorum.data = [...data.in_quorum];
      this.notInQuorum.data = [...data.out_quorum];
      this.mon_status = data.mon_status;
    });
  }
}
