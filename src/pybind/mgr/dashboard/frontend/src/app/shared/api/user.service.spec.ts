import { HttpClientTestingModule, HttpTestingController } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';

import { configureTestBed } from '../../../testing/unit-test-helper';
import { UserFormModel } from '../../core/auth/user-form/user-form.model';
import { UserService } from './user.service';

describe('UserService', () => {
  let service: UserService;
  let httpTesting: HttpTestingController;

  configureTestBed({
    providers: [UserService],
    imports: [HttpClientTestingModule]
  });

  beforeEach(() => {
    service = TestBed.get(UserService);
    httpTesting = TestBed.get(HttpTestingController);
  });

  afterEach(() => {
    httpTesting.verify();
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('should call create', () => {
    const user = new UserFormModel();
    user.username = 'user0';
    user.password = 'pass0';
    user.name = 'User 0';
    user.email = 'user0@email.com';
    user.roles = ['administrator'];
    service.create(user).subscribe();
    const req = httpTesting.expectOne('api/user');
    expect(req.request.method).toBe('POST');
    expect(req.request.body).toEqual(user);
  });

  it('should call delete', () => {
    service.delete('user0').subscribe();
    const req = httpTesting.expectOne('api/user/user0');
    expect(req.request.method).toBe('DELETE');
  });

  it('should call update', () => {
    const user = new UserFormModel();
    user.username = 'user0';
    user.password = 'pass0';
    user.name = 'User 0';
    user.email = 'user0@email.com';
    user.roles = ['administrator'];
    service.update(user).subscribe();
    const req = httpTesting.expectOne('api/user/user0');
    expect(req.request.body).toEqual(user);
    expect(req.request.method).toBe('PUT');
  });

  it('should call get', () => {
    service.get('user0').subscribe();
    const req = httpTesting.expectOne('api/user/user0');
    expect(req.request.method).toBe('GET');
  });

  it('should call list', () => {
    service.list().subscribe();
    const req = httpTesting.expectOne('api/user');
    expect(req.request.method).toBe('GET');
  });
});
