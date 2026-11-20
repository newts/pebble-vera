module.exports = [
  {
    type: 'heading',
    defaultValue: 'Vera Thermostat'
  },
  {
    type: 'text',
    defaultValue: 'Your phone talks to the Vera directly, so it must be on the same network (or VPN) as the Vera.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'input',
        messageKey: 'VeraIP',
        label: 'Vera IP address',
        defaultValue: '',
        attributes: {
          placeholder: '192.168.1.50',
          type: 'text'
        }
      },
      {
        type: 'input',
        messageKey: 'DeviceNum',
        label: 'Thermostat device number',
        defaultValue: '36',
        attributes: {
          type: 'number'
        }
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
