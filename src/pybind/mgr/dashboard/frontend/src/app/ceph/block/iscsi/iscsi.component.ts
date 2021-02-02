import { Component, OnInit, TemplateRef, ViewChild } from '@angular/core';

import { I18n } from '@ngx-translate/i18n-polyfill';

import { IscsiService } from '../../../shared/api/iscsi.service';
import { DimlessPipe } from '../../../shared/pipes/dimless.pipe';
import { IscsiBackstorePipe } from '../../../shared/pipes/iscsi-backstore.pipe';

@Component({
  selector: 'cd-iscsi',
  templateUrl: './iscsi.component.html',
  styleUrls: ['./iscsi.component.scss']
})
export class IscsiComponent implements OnInit {
  @ViewChild('statusColorTpl')
  statusColorTpl: TemplateRef<any>;
  @ViewChild('iscsiSparklineTpl')
  iscsiSparklineTpl: TemplateRef<any>;
  @ViewChild('iscsiPerSecondTpl')
  iscsiPerSecondTpl: TemplateRef<any>;
  @ViewChild('iscsiRelativeDateTpl')
  iscsiRelativeDateTpl: TemplateRef<any>;

  gateways = [];
  gatewaysColumns: any;
  images = [];
  imagesColumns: any;

  constructor(
    private iscsiService: IscsiService,
    private dimlessPipe: DimlessPipe,
    private iscsiBackstorePipe: IscsiBackstorePipe,
    private i18n: I18n
  ) {}

  ngOnInit() {
    this.gatewaysColumns = [
      {
        name: this.i18n('Name'),
        prop: 'name'
      },
      {
        name: this.i18n('State'),
        prop: 'state',
        cellTemplate: this.statusColorTpl
      },
      {
        name: this.i18n('# Targets'),
        prop: 'num_targets'
      },
      {
        name: this.i18n('# Sessions'),
        prop: 'num_sessions'
      }
    ];
    this.imagesColumns = [
      {
        name: this.i18n('Pool'),
        prop: 'pool'
      },
      {
        name: this.i18n('Image'),
        prop: 'image'
      },
      {
        name: this.i18n('Backstore'),
        prop: 'backstore',
        pipe: this.iscsiBackstorePipe
      },
      {
        name: this.i18n('Read Bytes'),
        prop: 'stats_history.rd_bytes',
        cellTemplate: this.iscsiSparklineTpl
      },
      {
        name: this.i18n('Write Bytes'),
        prop: 'stats_history.wr_bytes',
        cellTemplate: this.iscsiSparklineTpl
      },
      {
        name: this.i18n('Read Ops'),
        prop: 'stats.rd',
        pipe: this.dimlessPipe,
        cellTemplate: this.iscsiPerSecondTpl
      },
      {
        name: this.i18n('Write Ops'),
        prop: 'stats.wr',
        pipe: this.dimlessPipe,
        cellTemplate: this.iscsiPerSecondTpl
      },
      {
        name: this.i18n('A/O Since'),
        prop: 'optimized_since',
        cellTemplate: this.iscsiRelativeDateTpl
      }
    ];
  }

  refresh() {
    this.iscsiService.overview().subscribe((overview: Array<any>) => {
      this.gateways = overview['gateways'];
      this.images = overview['images'];
      this.images.map((image) => {
        if (image.stats_history) {
          image.stats_history.rd_bytes = image.stats_history.rd_bytes.map((i) => i[1]);
          image.stats_history.wr_bytes = image.stats_history.wr_bytes.map((i) => i[1]);
        }
        image.cdIsBinary = true;
        return image;
      });
    });
  }
}
