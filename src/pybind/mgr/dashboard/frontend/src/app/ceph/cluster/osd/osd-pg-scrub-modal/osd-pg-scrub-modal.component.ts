import { Component, ViewChild } from '@angular/core';

import { I18n } from '@ngx-translate/i18n-polyfill';
import { BsModalRef } from 'ngx-bootstrap/modal';
import { forkJoin as observableForkJoin } from 'rxjs';

import { ConfigOptionComponent } from '../../../../shared/components/config-option/config-option.component';
import { ActionLabelsI18n } from '../../../../shared/constants/app.constants';
import { NotificationType } from '../../../../shared/enum/notification-type.enum';
import { CdFormGroup } from '../../../../shared/forms/cd-form-group';
import { Permissions } from '../../../../shared/models/permissions';
import { AuthStorageService } from '../../../../shared/services/auth-storage.service';
import { NotificationService } from '../../../../shared/services/notification.service';
import { OsdPgScrubModalOptions } from './osd-pg-scrub-modal.options';

@Component({
  selector: 'cd-osd-pg-scrub-modal',
  templateUrl: './osd-pg-scrub-modal.component.html',
  styleUrls: ['./osd-pg-scrub-modal.component.scss']
})
export class OsdPgScrubModalComponent {
  osdPgScrubForm: CdFormGroup;
  action: string;
  resource: string;
  permissions: Permissions;

  @ViewChild('basicOptionsValues')
  basicOptionsValues: ConfigOptionComponent;
  basicOptions: Array<string> = OsdPgScrubModalOptions.basicOptions;

  @ViewChild('advancedOptionsValues')
  advancedOptionsValues: ConfigOptionComponent;
  advancedOptions: Array<string> = OsdPgScrubModalOptions.advancedOptions;

  advancedEnabled = false;

  constructor(
    public bsModalRef: BsModalRef,
    private authStorageService: AuthStorageService,
    private notificationService: NotificationService,
    private i18n: I18n,
    public actionLabels: ActionLabelsI18n
  ) {
    this.osdPgScrubForm = new CdFormGroup({});
    this.resource = this.i18n('PG scrub options');
    this.action = this.actionLabels.EDIT;
    this.permissions = this.authStorageService.getPermissions();
  }

  submitAction() {
    const observables = [this.basicOptionsValues.saveValues()];

    if (this.advancedOptionsValues) {
      observables.push(this.advancedOptionsValues.saveValues());
    }

    observableForkJoin(observables).subscribe(
      () => {
        this.notificationService.show(
          NotificationType.success,
          this.i18n('Updated PG scrub options')
        );
        this.bsModalRef.hide();
      },
      () => {
        this.bsModalRef.hide();
      }
    );
  }
}
