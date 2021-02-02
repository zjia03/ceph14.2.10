import { TestBed } from '@angular/core/testing';

import { configureTestBed } from '../../../testing/unit-test-helper';
import { TimeDiffService } from './time-diff.service';

describe('TimeDiffService', () => {
  let service: TimeDiffService;
  const baseTime = new Date('2022-02-22T00:00:00');

  configureTestBed({
    providers: [TimeDiffService]
  });

  beforeEach(() => {
    service = TestBed.get(TimeDiffService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });

  it('calculates a new date that happens after the given date', () => {
    expect(service.calculateDate(new Date('2022-02-28T04:05:00'), '2h')).toEqual(
      new Date('2022-02-28T06:05:00')
    );
    expect(service.calculateDate(baseTime, '15m')).toEqual(new Date('2022-02-22T00:15'));
    expect(service.calculateDate(baseTime, '5d 23h')).toEqual(new Date('2022-02-27T23:00'));
  });

  it('calculates a new date that happens before the given date', () => {
    expect(service.calculateDate(new Date('2022-02-22T02:00:00'), '2h', true)).toEqual(baseTime);
  });

  it('calculates the difference of two dates', () => {
    expect(
      service.calculateDuration(new Date('2022-02-22T00:45:00'), new Date('2022-02-22T02:00:00'))
    ).toBe('1h 15m');
    expect(service.calculateDuration(baseTime, new Date('2022-02-28T04:05:00'))).toBe('6d 4h 5m');
  });

  describe('testing duration calculation in detail', () => {
    const minutes = 60 * 1000;
    const hours = 60 * minutes;
    const days = 24 * hours;

    it('should allow different writings', () => {
      const expectDurationToBeMs = (duration, ms) =>
        expect(service['getDurationMs'](duration)).toBe(ms);
      expectDurationToBeMs('2h', 2 * hours);
      expectDurationToBeMs('4 Days', 4 * days);
      expectDurationToBeMs('3 minutes', 3 * minutes);
      expectDurationToBeMs('4 Days 2h 3 minutes', 4 * days + 2 * hours + 3 * minutes);
      expectDurationToBeMs('5d3h120m', 5 * days + 5 * hours);
    });

    it('should create duration string from ms', () => {
      const expectMsToBeDuration = (ms, duration) =>
        expect(service['getDuration'](ms)).toBe(duration);
      expectMsToBeDuration(2 * hours, '2h');
      expectMsToBeDuration(4 * days, '4d');
      expectMsToBeDuration(3 * minutes, '3m');
      expectMsToBeDuration(4 * days + 2 * hours + 3 * minutes, '4d 2h 3m');
      expectMsToBeDuration(service['getDurationMs']('5d3h120m'), '5d 5h');
    });
  });
});
