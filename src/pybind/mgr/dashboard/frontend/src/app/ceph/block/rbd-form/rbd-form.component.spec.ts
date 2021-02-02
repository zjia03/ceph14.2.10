import { HttpClientTestingModule } from '@angular/common/http/testing';
import { ComponentFixture, discardPeriodicTasks, fakeAsync, TestBed } from '@angular/core/testing';
import { ReactiveFormsModule } from '@angular/forms';
import { ActivatedRoute, Router } from '@angular/router';
import { RouterTestingModule } from '@angular/router/testing';
import { TooltipModule } from 'ngx-bootstrap/tooltip';

import { ToastrModule } from 'ngx-toastr';

import { By } from '@angular/platform-browser';
import { of } from 'rxjs';
import { delay } from 'rxjs/operators';

import { ActivatedRouteStub } from '../../../../testing/activated-route-stub';
import { configureTestBed, i18nProviders } from '../../../../testing/unit-test-helper';
import { RbdService } from '../../../shared/api/rbd.service';
import { SharedModule } from '../../../shared/shared.module';
import { RbdConfigurationFormComponent } from '../rbd-configuration-form/rbd-configuration-form.component';
import { RbdFormMode } from './rbd-form-mode.enum';
import { RbdFormComponent } from './rbd-form.component';

describe('RbdFormComponent', () => {
  let component: RbdFormComponent;
  let fixture: ComponentFixture<RbdFormComponent>;
  let activatedRoute: ActivatedRouteStub;

  const queryNativeElement = (cssSelector) =>
    fixture.debugElement.query(By.css(cssSelector)).nativeElement;

  configureTestBed({
    imports: [
      HttpClientTestingModule,
      ReactiveFormsModule,
      RouterTestingModule,
      ToastrModule.forRoot(),
      SharedModule,
      TooltipModule
    ],
    declarations: [RbdFormComponent, RbdConfigurationFormComponent],
    providers: [
      {
        provide: ActivatedRoute,
        useValue: new ActivatedRouteStub({ pool: 'foo', name: 'bar', snap: undefined })
      },
      i18nProviders,
      RbdService
    ]
  });

  beforeEach(() => {
    fixture = TestBed.createComponent(RbdFormComponent);
    component = fixture.componentInstance;
    activatedRoute = TestBed.get(ActivatedRoute);
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });

  describe('create/edit/clone/copy image', () => {
    let createAction;
    let editAction;
    let cloneAction;
    let copyAction;
    let rbdServiceGetSpy;

    beforeEach(() => {
      createAction = spyOn(component, 'createAction').and.stub();
      editAction = spyOn(component, 'editAction').and.stub();
      cloneAction = spyOn(component, 'cloneAction').and.stub();
      copyAction = spyOn(component, 'copyAction').and.stub();
      spyOn(component, 'setResponse').and.stub();
      spyOn(TestBed.get(Router), 'navigate').and.stub();
      rbdServiceGetSpy = spyOn(TestBed.get(RbdService), 'get');
      rbdServiceGetSpy.and.returnValue(of({ pool_name: 'foo', pool_image: 'bar' }));
      component.mode = undefined;
    });

    it('should create image', () => {
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(1);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(0);
    });

    it('should not edit image if no image data is received', fakeAsync(() => {
      component.mode = RbdFormMode.editing;
      rbdServiceGetSpy.and.returnValue(
        of({ pool_name: 'foo', pool_image: 'bar' }).pipe(delay(100))
      );
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(0);

      discardPeriodicTasks();
    }));

    it('should edit image after image data is received', () => {
      component.mode = RbdFormMode.editing;
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(1);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(0);
    });

    it('should not clone image if no image data is received', fakeAsync(() => {
      component.mode = RbdFormMode.cloning;
      rbdServiceGetSpy.and.returnValue(
        of({ pool_name: 'foo', pool_image: 'bar' }).pipe(delay(100))
      );
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(0);

      discardPeriodicTasks();
    }));

    it('should clone image after image data is received', () => {
      component.mode = RbdFormMode.cloning;
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(1);
      expect(copyAction).toHaveBeenCalledTimes(0);
    });

    it('should not copy image if no image data is received', fakeAsync(() => {
      component.mode = RbdFormMode.copying;
      rbdServiceGetSpy.and.returnValue(
        of({ pool_name: 'foo', pool_image: 'bar' }).pipe(delay(100))
      );
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(0);

      discardPeriodicTasks();
    }));

    it('should copy image after image data is received', () => {
      component.mode = RbdFormMode.copying;
      component.ngOnInit();
      component.submit();

      expect(createAction).toHaveBeenCalledTimes(0);
      expect(editAction).toHaveBeenCalledTimes(0);
      expect(cloneAction).toHaveBeenCalledTimes(0);
      expect(copyAction).toHaveBeenCalledTimes(1);
    });
  });

  describe('should test decodeURIComponent of params', () => {
    let rbdService: RbdService;

    beforeEach(() => {
      rbdService = TestBed.get(RbdService);
      component.mode = RbdFormMode.editing;
      fixture.detectChanges();
      spyOn(rbdService, 'get').and.callThrough();
    });

    it('without snapName', () => {
      activatedRoute.setParams({ pool: 'foo%2Ffoo', name: 'bar%2Fbar', snap: undefined });

      expect(rbdService.get).toHaveBeenCalledWith('foo/foo', 'bar/bar');
      expect(component.snapName).toBeUndefined();
    });

    it('with snapName', () => {
      activatedRoute.setParams({ pool: 'foo%2Ffoo', name: 'bar%2Fbar', snap: 'baz%2Fbaz' });

      expect(rbdService.get).toHaveBeenCalledWith('foo/foo', 'bar/bar');
      expect(component.snapName).toBe('baz/baz');
    });
  });

  describe('test image configuration component', () => {
    it('is visible', () => {
      fixture.detectChanges();
      expect(queryNativeElement('cd-rbd-configuration-form').parentElement.hidden).toBe(false);
    });
  });

  describe('tests for feature flags', () => {
    let deepFlatten, layering, exclusiveLock, objectMap, journaling, fastDiff;
    const defaultFeatures = [
      // Supposed to be enabled by default
      'deep-flatten',
      'exclusive-lock',
      'fast-diff',
      'layering',
      'object-map'
    ];
    const allFeatureNames = [
      'deep-flatten',
      'layering',
      'exclusive-lock',
      'object-map',
      'journaling',
      'fast-diff'
    ];
    const setFeatures = (features) => {
      component.features = features;
      component.featuresList = component.objToArray(features);
      component.createForm();
    };
    const getFeatureNativeElements = () => allFeatureNames.map((f) => queryNativeElement(`#${f}`));

    it('should convert feature flags correctly in the constructor', () => {
      setFeatures({
        one: { desc: 'one', allowEnable: true, allowDisable: true },
        two: { desc: 'two', allowEnable: true, allowDisable: true },
        three: { desc: 'three', allowEnable: true, allowDisable: true }
      });
      expect(component.featuresList).toEqual([
        { desc: 'one', key: 'one', allowDisable: true, allowEnable: true },
        { desc: 'two', key: 'two', allowDisable: true, allowEnable: true },
        { desc: 'three', key: 'three', allowDisable: true, allowEnable: true }
      ]);
    });

    describe('test edit form flags', () => {
      const prepare = (pool: string, image: string, enabledFeatures: string[]): void => {
        const rbdService = TestBed.get(RbdService);
        spyOn(rbdService, 'get').and.returnValue(
          of({
            name: image,
            pool_name: pool,
            features_name: enabledFeatures
          })
        );
        spyOn(rbdService, 'defaultFeatures').and.returnValue(of(defaultFeatures));
        component.router = { url: `/block/rbd/edit/${pool}/${image}` } as Router;
        fixture.detectChanges();
        [
          deepFlatten,
          layering,
          exclusiveLock,
          objectMap,
          journaling,
          fastDiff
        ] = getFeatureNativeElements();
      };

      it('should have the interlock feature for flags disabled, if one feature is not set', () => {
        prepare('rbd', 'foobar', ['deep-flatten', 'exclusive-lock', 'layering', 'object-map']);

        expect(objectMap.disabled).toBe(false);
        expect(fastDiff.disabled).toBe(false);

        expect(objectMap.checked).toBe(true);
        expect(fastDiff.checked).toBe(false);

        fastDiff.click();
        fastDiff.click();

        expect(objectMap.checked).toBe(true); // Shall not be disabled by `fast-diff`!
      });

      it('should not disable object-map when fast-diff is unchecked', () => {
        prepare('rbd', 'foobar', ['deep-flatten', 'exclusive-lock', 'layering', 'object-map']);

        fastDiff.click();
        fastDiff.click();

        expect(objectMap.checked).toBe(true); // Shall not be disabled by `fast-diff`!
      });

      it('should not enable fast-diff when object-map is checked', () => {
        prepare('rbd', 'foobar', ['deep-flatten', 'exclusive-lock', 'layering', 'object-map']);

        objectMap.click();
        objectMap.click();

        expect(fastDiff.checked).toBe(false); // Shall not be disabled by `fast-diff`!
      });
    });

    describe('test create form flags', () => {
      beforeEach(() => {
        const rbdService = TestBed.get(RbdService);
        spyOn(rbdService, 'defaultFeatures').and.returnValue(of(defaultFeatures));
        component.router = { url: '/block/rbd/create' } as Router;
        fixture.detectChanges();
        [
          deepFlatten,
          layering,
          exclusiveLock,
          objectMap,
          journaling,
          fastDiff
        ] = getFeatureNativeElements();
      });

      it('should initialize the checkboxes correctly', () => {
        expect(deepFlatten.disabled).toBe(false);
        expect(layering.disabled).toBe(false);
        expect(exclusiveLock.disabled).toBe(false);
        expect(objectMap.disabled).toBe(false);
        expect(journaling.disabled).toBe(false);
        expect(fastDiff.disabled).toBe(false);

        expect(deepFlatten.checked).toBe(true);
        expect(layering.checked).toBe(true);
        expect(exclusiveLock.checked).toBe(true);
        expect(objectMap.checked).toBe(true);
        expect(journaling.checked).toBe(false);
        expect(fastDiff.checked).toBe(true);
      });

      it('should disable features if their requirements are not met (exclusive-lock)', () => {
        exclusiveLock.click(); // unchecks exclusive-lock
        expect(objectMap.disabled).toBe(true);
        expect(journaling.disabled).toBe(true);
        expect(fastDiff.disabled).toBe(true);
      });

      it('should disable features if their requirements are not met (object-map)', () => {
        objectMap.click(); // unchecks object-map
        expect(fastDiff.disabled).toBe(true);
      });
    });
  });
});
