import { Component, OnInit, ViewChild } from '@angular/core';

import { BsModalRef, BsModalService } from 'ngx-bootstrap/modal';

import { CephReleaseNamePipe } from '../../../shared/pipes/ceph-release-name.pipe';
import { AuthStorageService } from '../../../shared/services/auth-storage.service';
import { SummaryService } from '../../../shared/services/summary.service';
import { AboutComponent } from '../about/about.component';

@Component({
  selector: 'cd-dashboard-help',
  templateUrl: './dashboard-help.component.html',
  styleUrls: ['./dashboard-help.component.scss']
})
export class DashboardHelpComponent implements OnInit {
  @ViewChild('docsForm')
  docsFormElement;
  docsUrl: string;
  modalRef: BsModalRef;

  constructor(
    private summaryService: SummaryService,
    private cephReleaseNamePipe: CephReleaseNamePipe,
    private modalService: BsModalService,
    private authStorageService: AuthStorageService
  ) {}

  ngOnInit() {
    const subs = this.summaryService.subscribe((summary: any) => {
      if (!summary) {
        return;
      }

      const releaseName = this.cephReleaseNamePipe.transform(summary.version);
      this.docsUrl = `http://docs.ceph.com/docs/${releaseName}/mgr/dashboard/`;

      setTimeout(() => {
        subs.unsubscribe();
      }, 0);
    });
  }

  openAboutModal() {
    this.modalRef = this.modalService.show(AboutComponent);
  }

  goToApiDocs() {
    const tokenInput = this.docsFormElement.nativeElement.children[0];
    tokenInput.value = this.authStorageService.getToken();
    this.docsFormElement.nativeElement.submit();
  }
}
