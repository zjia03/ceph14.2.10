import { HttpClientTestingModule, HttpTestingController } from '@angular/common/http/testing';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { ReactiveFormsModule } from '@angular/forms';
import { RouterTestingModule } from '@angular/router/testing';

import * as moment from 'moment';
import { BsDatepickerModule } from 'ngx-bootstrap/datepicker';
import { BsModalRef, BsModalService } from 'ngx-bootstrap/modal';
import { ToastrModule } from 'ngx-toastr';

import { configureTestBed, i18nProviders } from '../../../../testing/unit-test-helper';
import { NotificationService } from '../../../shared/services/notification.service';
import { SharedModule } from '../../../shared/shared.module';
import { RbdTrashMoveModalComponent } from './rbd-trash-move-modal.component';

describe('RbdTrashMoveModalComponent', () => {
  let component: RbdTrashMoveModalComponent;
  let fixture: ComponentFixture<RbdTrashMoveModalComponent>;
  let httpTesting: HttpTestingController;

  configureTestBed({
    imports: [
      ReactiveFormsModule,
      HttpClientTestingModule,
      RouterTestingModule,
      SharedModule,
      ToastrModule.forRoot(),
      BsDatepickerModule.forRoot()
    ],
    declarations: [RbdTrashMoveModalComponent],
    providers: [BsModalRef, BsModalService, i18nProviders]
  });

  beforeEach(() => {
    fixture = TestBed.createComponent(RbdTrashMoveModalComponent);
    component = fixture.componentInstance;
    httpTesting = TestBed.get(HttpTestingController);

    component.metaType = 'RBD';
    component.poolName = 'foo';
    component.imageName = 'bar';
  });

  it('should create', () => {
    expect(component).toBeTruthy();
    expect(component.moveForm).toBeDefined();
  });

  it('should finish running ngOnInit', () => {
    fixture.detectChanges();
    expect(component.pattern).toEqual('foo/bar');
  });

  describe('should call moveImage', () => {
    let notificationService;

    beforeEach(() => {
      notificationService = TestBed.get(NotificationService);
      spyOn(notificationService, 'show').and.stub();
      spyOn(component.modalRef, 'hide').and.callThrough();
    });

    afterEach(() => {
      expect(notificationService.show).toHaveBeenCalledTimes(1);
      expect(component.modalRef.hide).toHaveBeenCalledTimes(1);
    });

    it('with normal delay', () => {
      component.moveImage();
      const req = httpTesting.expectOne('api/block/image/foo/bar/move_trash');
      req.flush(null);
      expect(req.request.body).toEqual({ delay: 0 });
    });

    it('with delay < 0', () => {
      const oldDate = moment()
        .subtract(24, 'hour')
        .toDate();
      component.moveForm.patchValue({ expiresAt: oldDate });

      component.moveImage();
      const req = httpTesting.expectOne('api/block/image/foo/bar/move_trash');
      req.flush(null);
      expect(req.request.body).toEqual({ delay: 0 });
    });

    it('with delay < 0', () => {
      const oldDate = moment()
        .add(24, 'hour')
        .toISOString();
      fixture.detectChanges();
      component.moveForm.patchValue({ expiresAt: oldDate });

      component.moveImage();
      const req = httpTesting.expectOne('api/block/image/foo/bar/move_trash');
      req.flush(null);
      expect(req.request.body.delay).toBeGreaterThan(86390);
    });
  });
});
