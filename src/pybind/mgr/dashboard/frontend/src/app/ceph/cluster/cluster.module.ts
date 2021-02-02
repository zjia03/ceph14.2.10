import { CommonModule } from '@angular/common';
import { NgModule } from '@angular/core';
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { RouterModule } from '@angular/router';

import { TreeModule } from 'ng2-tree';
import { AlertModule } from 'ngx-bootstrap/alert';
import { BsDatepickerModule } from 'ngx-bootstrap/datepicker';
import { BsDropdownModule } from 'ngx-bootstrap/dropdown';
import { ModalModule } from 'ngx-bootstrap/modal';
import { TabsModule } from 'ngx-bootstrap/tabs';
import { TimepickerModule } from 'ngx-bootstrap/timepicker';
import { TooltipModule } from 'ngx-bootstrap/tooltip';
import { TypeaheadModule } from 'ngx-bootstrap/typeahead';

import { SharedModule } from '../../shared/shared.module';
import { PerformanceCounterModule } from '../performance-counter/performance-counter.module';
import { ConfigurationDetailsComponent } from './configuration/configuration-details/configuration-details.component';
import { ConfigurationFormComponent } from './configuration/configuration-form/configuration-form.component';
import { ConfigurationComponent } from './configuration/configuration.component';
import { CrushmapComponent } from './crushmap/crushmap.component';
import { HostDetailsComponent } from './hosts/host-details/host-details.component';
import { HostsComponent } from './hosts/hosts.component';
import { LogsComponent } from './logs/logs.component';
import { MgrModulesModule } from './mgr-modules/mgr-modules.module';
import { MonitorComponent } from './monitor/monitor.component';
import { OsdDetailsComponent } from './osd/osd-details/osd-details.component';
import { OsdFlagsModalComponent } from './osd/osd-flags-modal/osd-flags-modal.component';
import { OsdListComponent } from './osd/osd-list/osd-list.component';
import { OsdPerformanceHistogramComponent } from './osd/osd-performance-histogram/osd-performance-histogram.component';
import { OsdPgScrubModalComponent } from './osd/osd-pg-scrub-modal/osd-pg-scrub-modal.component';
import { OsdRecvSpeedModalComponent } from './osd/osd-recv-speed-modal/osd-recv-speed-modal.component';
import { OsdReweightModalComponent } from './osd/osd-reweight-modal/osd-reweight-modal.component';
import { OsdScrubModalComponent } from './osd/osd-scrub-modal/osd-scrub-modal.component';
import { ActiveAlertListComponent } from './prometheus/active-alert-list/active-alert-list.component';
import { MonitoringListComponent } from './prometheus/monitoring-list/monitoring-list.component';
import { RulesListComponent } from './prometheus/rules-list/rules-list.component';
import { SilenceFormComponent } from './prometheus/silence-form/silence-form.component';
import { SilenceListComponent } from './prometheus/silence-list/silence-list.component';
import { SilenceMatcherModalComponent } from './prometheus/silence-matcher-modal/silence-matcher-modal.component';

@NgModule({
  entryComponents: [
    OsdDetailsComponent,
    OsdScrubModalComponent,
    OsdFlagsModalComponent,
    OsdRecvSpeedModalComponent,
    OsdReweightModalComponent,
    OsdPgScrubModalComponent,
    OsdReweightModalComponent,
    SilenceMatcherModalComponent
  ],
  imports: [
    CommonModule,
    PerformanceCounterModule,
    TabsModule.forRoot(),
    SharedModule,
    RouterModule,
    FormsModule,
    ReactiveFormsModule,
    BsDropdownModule.forRoot(),
    BsDatepickerModule.forRoot(),
    ModalModule.forRoot(),
    AlertModule.forRoot(),
    TooltipModule.forRoot(),
    TreeModule,
    MgrModulesModule,
    TypeaheadModule.forRoot(),
    TimepickerModule.forRoot(),
    BsDatepickerModule.forRoot()
  ],
  declarations: [
    HostsComponent,
    MonitorComponent,
    ConfigurationComponent,
    OsdListComponent,
    OsdDetailsComponent,
    OsdPerformanceHistogramComponent,
    OsdScrubModalComponent,
    OsdFlagsModalComponent,
    HostDetailsComponent,
    ConfigurationDetailsComponent,
    ConfigurationFormComponent,
    OsdReweightModalComponent,
    CrushmapComponent,
    LogsComponent,
    OsdRecvSpeedModalComponent,
    OsdPgScrubModalComponent,
    ActiveAlertListComponent,
    OsdRecvSpeedModalComponent,
    SilenceFormComponent,
    SilenceListComponent,
    SilenceMatcherModalComponent,
    RulesListComponent,
    MonitoringListComponent
  ]
})
export class ClusterModule {}
