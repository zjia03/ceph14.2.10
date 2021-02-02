import { Component, OnInit } from '@angular/core';
import { FormControl } from '@angular/forms';

import * as _ from 'lodash';
import { BsModalRef } from 'ngx-bootstrap/modal';

import { IscsiService } from '../../../shared/api/iscsi.service';
import { CdFormGroup } from '../../../shared/forms/cd-form-group';

@Component({
  selector: 'cd-iscsi-target-iqn-settings-modal',
  templateUrl: './iscsi-target-iqn-settings-modal.component.html',
  styleUrls: ['./iscsi-target-iqn-settings-modal.component.scss']
})
export class IscsiTargetIqnSettingsModalComponent implements OnInit {
  target_controls: FormControl;
  target_default_controls: any;
  target_controls_limits: any;

  settingsForm: CdFormGroup;

  constructor(public modalRef: BsModalRef, public iscsiService: IscsiService) {}

  ngOnInit() {
    const fg = {};
    _.forIn(this.target_default_controls, (_value, key) => {
      fg[key] = new FormControl(this.target_controls.value[key]);
    });

    this.settingsForm = new CdFormGroup(fg);
  }

  save() {
    const settings = {};
    _.forIn(this.settingsForm.controls, (control, key) => {
      if (!(control.value === '' || control.value === null)) {
        settings[key] = control.value;
      }
    });

    this.target_controls.setValue(settings);
    this.modalRef.hide();
  }

  getTargetControlLimits(setting) {
    if (this.target_controls_limits) {
      return this.target_controls_limits[setting];
    }
    // backward compatibility
    if (['Yes', 'No'].includes(this.target_default_controls[setting])) {
      return { type: 'bool' };
    }
    return { type: 'int' };
  }
}
