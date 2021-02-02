import { Pipe, PipeTransform } from '@angular/core';

import { I18n } from '@ngx-translate/i18n-polyfill';

@Pipe({
  name: 'notAvailable'
})
export class NotAvailablePipe implements PipeTransform {
  constructor(private i18n: I18n) {}
  transform(value: any): any {
    if (value === '') {
      return this.i18n('n/a');
    }
    return value;
  }
}
